#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "My_Common.h"
#include "Integrator.h"
#include "PostProcess.h"

// Arbitrary output variables (AOVs) for debugging ("why is this pixel black?").
//
// All AOVs are accumulated per sample from PathSample records, in linear units, and written as
// PFM (exact values) plus a PPM visualization. The visualization maps the data to a viewable
// range (normals to [0,1], scalars through a normalized color map) without changing the PFM.
enum class AOVKind : int
{
	Beauty,
	Albedo,
	ShadingNormal,
	GeometryNormal,
	Depth,
	Roughness,
	Metallic,
	Emission,
	Direct,
	Indirect,
	SampleCount,
	BsdfPdf,
	LightPdf,
	MisWeight,
	PathLength,
	Variance,
	Count
};

inline const char* aov_name(AOVKind kind)
{
	switch (kind)
	{
	case AOVKind::Beauty: return "beauty";
	case AOVKind::Albedo: return "albedo";
	case AOVKind::ShadingNormal: return "normal";
	case AOVKind::GeometryNormal: return "geo_normal";
	case AOVKind::Depth: return "depth";
	case AOVKind::Roughness: return "roughness";
	case AOVKind::Metallic: return "metallic";
	case AOVKind::Emission: return "emission";
	case AOVKind::Direct: return "direct";
	case AOVKind::Indirect: return "indirect";
	case AOVKind::SampleCount: return "samples";
	case AOVKind::BsdfPdf: return "bsdf_pdf";
	case AOVKind::LightPdf: return "light_pdf";
	case AOVKind::MisWeight: return "mis_weight";
	case AOVKind::PathLength: return "path_length";
	case AOVKind::Variance: return "variance";
	case AOVKind::Count: break;
	}
	return "unknown";
}

// Parses "normal,depth" or "all". Returns false on an unknown name.
inline bool parse_aov_list(const std::string& text, std::vector<AOVKind>& out)
{
	out.clear();
	size_t start = 0;
	while (start <= text.size())
	{
		const size_t comma = text.find(',', start);
		const std::string name = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		if (name == "all")
		{
			for (int k = 0; k < static_cast<int>(AOVKind::Count); ++k)
				out.push_back(static_cast<AOVKind>(k));
		}
		else if (!name.empty())
		{
			bool found = false;
			for (int k = 0; k < static_cast<int>(AOVKind::Count); ++k)
			{
				if (name == aov_name(static_cast<AOVKind>(k)))
				{
					out.push_back(static_cast<AOVKind>(k));
					found = true;
				}
			}
			if (!found)
				return false;
		}
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return true;
}

// Per-pixel accumulation. Every pixel is written by exactly one thread (in sample order), so no
// synchronization is needed and the result is deterministic.
class AOVBuffers
{
public:
	void reset(size_t pixel_count, const std::vector<AOVKind>& requested)
	{
		pixels = pixel_count;
		enabled.fill(false);
		for (AOVKind k : requested)
			enabled[static_cast<size_t>(k)] = true;
		for (size_t k = 0; k < sums.size(); ++k)
			sums[k].assign(enabled[k] ? pixel_count * 3 : 0, 0.0f);
		hit_count.assign(any_first_hit_enabled() ? pixel_count : 0, 0u);

		// Always on: luminance moments (variance) and the denoiser guide.
		mean.assign(pixel_count, 0.0);
		m2.assign(pixel_count, 0.0);
		sample_count.assign(pixel_count, 0u);
		guide_albedo.assign(pixel_count * 3, 0.0f);
		guide_normal.assign(pixel_count * 3, 0.0f);
		guide_depth.assign(pixel_count, 0.0f);
		guide_gloss.assign(pixel_count, 0.0f);
		guide_count.assign(pixel_count, 0u);
		guide_emitter.assign(pixel_count, 0u);
	}

	bool is_enabled(AOVKind k) const { return enabled[static_cast<size_t>(k)]; }

	void accumulate(size_t index, const PathSample& s)
	{
		// Welford's online mean / variance of the per-sample luminance.
		const double lum = luminance(s.L);
		const uint32_t n = ++sample_count[index];
		const double delta = lum - mean[index];
		mean[index] += delta / n;
		m2[index] += delta * (lum - mean[index]);

		if (s.guide_valid)
		{
			add3(guide_albedo, index, s.guide_albedo);
			add3(guide_normal, index, s.guide_normal);
			guide_depth[index] += static_cast<float>(s.guide_depth);
			guide_gloss[index] += static_cast<float>(s.guide_glossiness);
			guide_count[index]++;
			if (s.guide_emitter)
				guide_emitter[index]++;
		}

		add(AOVKind::Beauty, index, s.L);
		add(AOVKind::Emission, index, s.emission);
		add(AOVKind::Direct, index, s.direct);
		add(AOVKind::Indirect, index, s.indirect);
		add(AOVKind::Albedo, index, s.albedo);
		add(AOVKind::BsdfPdf, index, scalar(s.bsdf_pdf));
		add(AOVKind::LightPdf, index, scalar(s.light_pdf));
		add(AOVKind::MisWeight, index, scalar(s.mis_weight));
		add(AOVKind::PathLength, index, scalar(static_cast<double>(s.path_length)));
		if (s.hit)
		{
			if (!hit_count.empty())
				hit_count[index]++;
			add(AOVKind::ShadingNormal, index, s.shading_normal);
			add(AOVKind::GeometryNormal, index, s.geometry_normal);
			add(AOVKind::Depth, index, scalar(s.depth));
			add(AOVKind::Roughness, index, scalar(s.roughness));
			add(AOVKind::Metallic, index, scalar(s.metallic));
		}
	}

	// Variance of the pixel mean (luminance): sample variance / n.
	double variance_of_mean(size_t index) const
	{
		const uint32_t n = sample_count[index];
		if (n < 2)
			return 0.0;
		return m2[index] / (static_cast<double>(n) - 1.0) / n;
	}

	PixelGuide guide(size_t index) const
	{
		PixelGuide g;
		const uint32_t n = guide_count[index];
		g.sample_count = static_cast<int>(sample_count[index]);
		g.variance = variance_of_mean(index);
		if (n == 0)
			return g;
		const double inv = 1.0 / n;
		g.valid = true;
		g.albedo = get3(guide_albedo, index) * inv;
		const Vector3 nrm = get3(guide_normal, index);
		g.normal = nrm.length_squared() > 0.0 ? normalize(nrm) : Vector3(0, 0, 0);
		g.depth = guide_depth[index] * inv;
		g.glossiness = guide_gloss[index] * inv;
		// Fraction of samples that saw geometry (edges against the background are partial).
		g.coverage = static_cast<double>(n) / std::max<uint32_t>(1, sample_count[index]);
		g.emitter = guide_emitter[index] > 0;
		return g;
	}

	// Linear value of an AOV (per-sample average; first-hit AOVs averaged over hit samples).
	std::vector<Color> resolve(AOVKind kind) const
	{
		std::vector<Color> out(pixels, Color(0, 0, 0));
		if (kind == AOVKind::SampleCount)
		{
			for (size_t i = 0; i < pixels; ++i)
				out[i] = scalar(sample_count[i]);
			return out;
		}
		if (kind == AOVKind::Variance)
		{
			for (size_t i = 0; i < pixels; ++i)
				out[i] = scalar(variance_of_mean(i));
			return out;
		}
		if (!is_enabled(kind))
			return out;
		const std::vector<float>& s = sums[static_cast<size_t>(kind)];
		for (size_t i = 0; i < pixels; ++i)
		{
			const double n = first_hit(kind) ? hit_count[i] : sample_count[i];
			if (n > 0)
				out[i] = Color(s[3 * i], s[3 * i + 1], s[3 * i + 2]) / n;
		}
		return out;
	}

	// 8-bit visualization of an AOV (row-major RGB).
	static std::vector<unsigned char> visualize(AOVKind kind, const std::vector<Color>& data, double max_depth_hint)
	{
		std::vector<unsigned char> bytes(data.size() * 3, 0);
		auto put = [&](size_t i, const Color& c)
		{
			for (int k = 0; k < 3; ++k)
			{
				const double v = std::isfinite(c[k]) ? std::clamp(c[k], 0.0, 1.0) : 0.0;
				bytes[3 * i + k] = static_cast<unsigned char>(std::lround(255.0 * v));
			}
		};

		switch (kind)
		{
		case AOVKind::Beauty:
		case AOVKind::Emission:
		case AOVKind::Direct:
		case AOVKind::Indirect:
			for (size_t i = 0; i < data.size(); ++i)
			{
				const Color_Bytes b = to_color_bytes(data[i]);
				bytes[3 * i] = b.r; bytes[3 * i + 1] = b.g; bytes[3 * i + 2] = b.b;
			}
			return bytes;
		case AOVKind::Albedo:
			for (size_t i = 0; i < data.size(); ++i)
				put(i, Color(linear_to_srgb(data[i].x()), linear_to_srgb(data[i].y()), linear_to_srgb(data[i].z())));
			return bytes;
		case AOVKind::ShadingNormal:
		case AOVKind::GeometryNormal:
			for (size_t i = 0; i < data.size(); ++i)
				put(i, data[i].length_squared() > 0.0 ? 0.5 * (data[i] + Color(1, 1, 1)) : Color(0, 0, 0));
			return bytes;
		case AOVKind::Roughness:
		case AOVKind::Metallic:
		case AOVKind::MisWeight:
			for (size_t i = 0; i < data.size(); ++i)
				put(i, colormap(data[i].x()));
			return bytes;
		default:
			break;
		}

		// Remaining scalars: normalize by a robust maximum (99th percentile), log scale for pdfs.
		const bool log_scale = kind == AOVKind::BsdfPdf || kind == AOVKind::LightPdf || kind == AOVKind::Variance;
		std::vector<double> values;
		values.reserve(data.size());
		for (const Color& c : data)
			if (std::isfinite(c.x()) && c.x() > 0.0)
				values.push_back(log_scale ? std::log10(1.0 + c.x()) : c.x());
		double hi = 1.0;
		if (kind == AOVKind::PathLength && max_depth_hint > 0.0)
			hi = max_depth_hint;
		else if (!values.empty())
		{
			std::nth_element(values.begin(), values.begin() + static_cast<long long>(0.99 * (values.size() - 1)), values.end());
			hi = std::max(1e-12, values[static_cast<size_t>(0.99 * (values.size() - 1))]);
		}
		for (size_t i = 0; i < data.size(); ++i)
		{
			double v = data[i].x();
			if (!std::isfinite(v) || v <= 0.0)
				continue;
			if (log_scale)
				v = std::log10(1.0 + v);
			v /= hi;
			if (kind == AOVKind::Depth)
				v = 1.0 - v; // near = bright
			put(i, colormap(v));
		}
		return bytes;
	}

private:
	size_t pixels = 0;
	std::array<bool, static_cast<size_t>(AOVKind::Count)> enabled{};
	std::array<std::vector<float>, static_cast<size_t>(AOVKind::Count)> sums;
	std::vector<uint32_t> hit_count;
	std::vector<double> mean;
	std::vector<double> m2;
	std::vector<uint32_t> sample_count;
	std::vector<float> guide_albedo;
	std::vector<float> guide_normal;
	std::vector<float> guide_depth;
	std::vector<float> guide_gloss;
	std::vector<uint32_t> guide_count;
	std::vector<uint32_t> guide_emitter;

	static Color scalar(double v) { return Color(v, v, v); }

	static bool first_hit(AOVKind k)
	{
		return k == AOVKind::ShadingNormal || k == AOVKind::GeometryNormal || k == AOVKind::Depth ||
			k == AOVKind::Roughness || k == AOVKind::Metallic;
	}

	bool any_first_hit_enabled() const
	{
		for (int k = 0; k < static_cast<int>(AOVKind::Count); ++k)
			if (enabled[static_cast<size_t>(k)] && first_hit(static_cast<AOVKind>(k)))
				return true;
		return false;
	}

	void add(AOVKind kind, size_t index, const Color& c)
	{
		if (enabled[static_cast<size_t>(kind)])
			add3(sums[static_cast<size_t>(kind)], index, c);
	}

	static void add3(std::vector<float>& v, size_t index, const Color& c)
	{
		v[3 * index + 0] += static_cast<float>(c.x());
		v[3 * index + 1] += static_cast<float>(c.y());
		v[3 * index + 2] += static_cast<float>(c.z());
	}

	static Color get3(const std::vector<float>& v, size_t index)
	{
		return Color(v[3 * index], v[3 * index + 1], v[3 * index + 2]);
	}

	// Compact "turbo"-like color map (Mikhailov 2019, polynomial approximation), t in [0, 1].
	static Color colormap(double t)
	{
		t = std::clamp(t, 0.0, 1.0);
		const double r = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234 + t * (-152.94239396 + t * 59.28637943))));
		const double g = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333 + t * (4.27729857 + t * 2.82956604))));
		const double b = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771 + t * (-89.90310912 + t * 27.34824973))));
		return Color(std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0));
	}
};
