#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "Color.h"
#include "My_Common.h"
#include "rtw_stb_image.h"

namespace environment_detail
{
	// Coarse inverse-CDF brackets accelerate binary search without changing its answer.
	// CDF values, accumulation order and random-number consumption stay unchanged.
	class CdfSearchGuide
	{
	public:
		static constexpr int kBins = 64;

		void build(const std::vector<double>& cdf)
		{
			for (int k = 0; k <= kBins; ++k)
				first[k] = static_cast<int>(std::lower_bound(cdf.begin(), cdf.end(),
					static_cast<double>(k) / kBins) - cdf.begin());
		}

		// u is a uniform variate in [0, 1]. Return the full-CDF index, including size()
		// when no entry matches, just as lower_bound does. The caller retains its clamp.
		int lower_bound(const std::vector<double>& cdf, double u) const
		{
			const int bin = std::clamp(static_cast<int>(u * kBins), 0, kBins - 1);
			const int end = std::min(first[bin + 1] + 1, static_cast<int>(cdf.size()));
			const auto it = std::lower_bound(cdf.begin() + first[bin], cdf.begin() + end, u);
			return static_cast<int>(it - cdf.begin());
		}

	private:
		std::array<int, kBins + 1> first{};
	};
}

class Environment
{
public:
	// Stack-local cache for a PDF/radiance pair. It owns no data and is never shared
	// by render threads. Lat-long lookups reuse UVs only for the same normalized direction.
	struct LookupCache
	{
		const Environment* owner = nullptr;
		// Payload is read only after owner matches. Avoid zeroing unused UV/direction
		// storage at every path vertex, particularly in scenes with no environment.
		std::array<double, 3> direction;
		double u, v;
	};

	virtual ~Environment() = default;
	virtual Color radiance(const Vector3& dir) const = 0;
	virtual double pdf_value(const Vector3& dir) const = 0;
	virtual Color radiance(const Vector3& dir, LookupCache& cache) const { return radiance(dir); }
	virtual double pdf_value(const Vector3& dir, LookupCache& cache) const { return pdf_value(dir); }
	virtual Vector3 random() const = 0;
	// Integral of luminance(L(w)) over the full sphere of directions. Resolution independent;
	// used by LightSampler to compare the environment with geometry lights (same units).
	virtual double integrated_luminance() const = 0;
};

// Uniform radiance from every direction. Used by white-furnace tests, where a convex white
// object under L = 1 must render exactly 1 everywhere.
class Constant_Environment : public Environment
{
public:
	explicit Constant_Environment(const Color& L) : L(L) {}

	Color radiance(const Vector3&) const override { return L; }
	double pdf_value(const Vector3&) const override { return 1.0 / (4.0 * pi); }
	Vector3 random() const override { return random_unit_vector(); }
	double integrated_luminance() const override
	{
		return (0.2126 * L.x() + 0.7152 * L.y() + 0.0722 * L.z()) * 4.0 * pi;
	}

private:
	Color L;
};

class LatLong_Environment : public Environment
{
public:
	// Lat-long HDRI environment. It is sampled as visible background and as an importance-sampled light.
	LatLong_Environment(
		const std::string& filename,
		double intensity = 1.0,
		double rotation = 0.0,
		bool srgb_input = false)
		: image(filename.c_str()),
		  intensity(intensity),
		  rotation(rotation),
		  srgb_input(srgb_input)
	{
		build_sampling_distribution();
	}

	Color radiance(const Vector3& dir) const override
	{
		return radiance_impl(dir, nullptr);
	}

	Color radiance(const Vector3& dir, LookupCache& cache) const override
	{
		return radiance_impl(dir, &cache);
	}

	double pdf_value(const Vector3& dir) const override
	{
		return pdf_impl(dir, nullptr);
	}

	double pdf_value(const Vector3& dir, LookupCache& cache) const override
	{
		return pdf_impl(dir, &cache);
	}

private:
	Color radiance_impl(const Vector3& dir, LookupCache* cache) const
	{
		// Convert ray direction to HDR texture lookup.
		if (!image.is_valid())
			return Color(1.0, 0.0, 1.0);

		const auto [u, v] = direction_to_uv(dir, cache);
		return sample_bilinear(u, v) * intensity;
	}

	double pdf_impl(const Vector3& dir, LookupCache* cache) const
	{
		// Degrade to uniform sampling if the environment map is invalid or has no contribution
		if (!image.is_valid() || width <= 0 || height <= 0 || total_weight <= 0.0)
			return 1.0 / (4.0 * pi);

		const Vector3 d = normalize(dir);
		auto [u, v] = direction_to_uv(d, cache);

		int x = static_cast<int>(u * width);
		int y = static_cast<int>(v * height);

		x = std::clamp(x, 0, width - 1);
		y = std::clamp(y, 0, height - 1);

		const double p_texel = texel_probability(x, y);
		
		// Compute the area of the texel in UV space
		const double texel_area_uv = 1.0 / (static_cast<double>(width) * static_cast<double>(height));
		// Convert the texel probability to a PDF value in uv space
		const double pdf_uv = p_texel / texel_area_uv;

		// Convert UV-space PDF to solid-angle PDF. The path tracer divides by this value.
		// lat-long: domega = 2*pi*pi*sin(theta) du dv
		const double theta = v * pi;
		const double sin_theta = std::max(std::sin(theta), 1e-6);

		const double pdf_omega = pdf_uv / (2.0 * pi * pi * sin_theta);
		return pdf_omega;
	}

public:
	Vector3 random() const override
	{
		if (!image.is_valid() || width <= 0 || height <= 0 || total_weight <= 0.0)
			return random_unit_vector();

		// Get a random coordinate (x, y) from the precomputed luminance CDFs.
		const auto [x, y] = sample_pixel_indices();

		// Jittering the uv in the texel
		const double u = (static_cast<int>(x) + random_double()) / static_cast<double>(width);
		const double v = (static_cast<int>(y) + random_double()) / static_cast<double>(height);

		return uv_to_direction(u, v);
	}

	// total_weight = sum over texels of lum * sin(theta); a texel covers
	// 2 pi^2 sin(theta) / (W H) steradians, so integral L dw = intensity * total_weight * 2 pi^2 / (W H).
	// (The previous estimate returned intensity * total_weight, a pixel sum that grows with the
	// texture resolution and cannot be compared with geometry lights.)
	double integrated_luminance() const override
	{
		if (width <= 0 || height <= 0)
			return 0.0;
		return intensity * total_weight * 2.0 * pi * pi / (static_cast<double>(width) * static_cast<double>(height));
	}

private:
	rtw_image image;
	double intensity = 1.0;
	double rotation = 0.0;
	bool srgb_input = false;

	int width = 0;
	int height = 0;
	std::vector<double> marginal_cdf;                  // marginal_cdf[y]: CDF for y column selection
	std::vector<std::vector<double>> conditional_cdf;  // conditional_cdf[y][x]: CDF for x column selection given y row
	environment_detail::CdfSearchGuide marginal_guide;
	std::vector<environment_detail::CdfSearchGuide> conditional_guides;
	std::vector<double> row_integrals;                 // row_integrals[y]: Integral of the y row (sum of luminance values in the row)
	// Immutable texel PMF, shared by every render thread. Avoid decoding the image and
	// recomputing luminance / row sin(theta) for every light-mixture PDF query.
	std::vector<double> texel_probabilities;
	double total_weight = 0.0;                         // total_weight: Integral of the entire environment map (sum of all row integrals)

	static double srgb_to_linear(double x)
	{
		if (x <= 0.04045)
			return x / 12.92;
		return std::pow((x + 0.055) / 1.055, 2.4);
	}

	Color decode_if_needed(const Color& c) const
	{
		if (!srgb_input)
			return c;

		return Color(
			srgb_to_linear(c.x()),
			srgb_to_linear(c.y()),
			srgb_to_linear(c.z()));
	}

	// Convert a 3D direction vector to 2D UV coordinates for sampling the lat-long environment map
	// Formula: u = (phi + pi + rotation) / (2 * pi), v = theta / pi
	std::pair<double, double> direction_to_uv(const Vector3& dir, LookupCache* cache) const
	{
		const Vector3 d = normalize(dir);
		// PDF historically normalizes twice, radiance once. Do not approximate equality:
		// even a tiny change can select a different texel at a boundary. Signed zero also
		// matters for atan2 at the longitude seam.
		if (cache && cache->owner == this
			&& std::memcmp(d.e.data(), cache->direction.data(), 3 * sizeof(double)) == 0)
			return { cache->u, cache->v };

		double phi = std::atan2(d.z(), d.x());
		double theta = std::acos(std::clamp(d.y(), -1.0, 1.0));

		double u = (phi + pi + rotation) / (2.0 * pi);
		double v = theta / pi;

		u = std::fmod(u, 1.0);
		if (u < 0.0)
			u += 1.0;
		v = std::clamp(v, 0.0, 1.0);
		if (cache)
		{
			cache->owner = this;
			cache->direction = d.e;
			cache->u = u;
			cache->v = v;
		}

		return { u, v };
	}

	Color sample_bilinear(double u, double v) const
	{
		if (!image.is_valid())
			return Color(1.0, 0.0, 1.0);

		const double x = u * (image.width() - 1);
		const double y = v * (image.height() - 1);

		const int x0 = static_cast<int>(std::floor(x));
		const int y0 = static_cast<int>(std::floor(y));
		const int x1 = (x0 + 1) % image.width();
		const int y1 = std::min(y0 + 1, image.height() - 1);

		const double tx = x - x0;
		const double ty = y - y0;

		const Color c00 = decode_if_needed(image.float_pixel(x0, y0));
		const Color c10 = decode_if_needed(image.float_pixel(x1, y0));
		const Color c01 = decode_if_needed(image.float_pixel(x0, y1));
		const Color c11 = decode_if_needed(image.float_pixel(x1, y1));

		const Color cx0 = (1.0 - tx) * c00 + tx * c10;
		const Color cx1 = (1.0 - tx) * c01 + tx * c11;
		return (1.0 - ty) * cx0 + ty * cx1;
	}

	// Luminance decides which texels should be sampled more often.
	double luminance(const Color& c) const
	{
		return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z();
	}

	Vector3 uv_to_direction(double u, double v) const
	{
		double phi = u * 2.0 * pi - pi - rotation;
		double theta = v * pi;

		double x = cos(phi) * sin(theta);
		double y = cos(theta);
		double z = sin(phi) * sin(theta);

		return normalize(Vector3(x, y, z));
	}

	// Precompute the sampling distribution for importance sampling during initialization
	// Two-level CDF: first pick a row, then pick a texel inside that row
	void build_sampling_distribution()
	{
		width = image.width();
		height = image.height();
		
		// Clear and resize the CDF and integral arrays
		row_integrals.assign(height, 0.0);
		marginal_cdf.assign(height, 0.0);
		conditional_cdf.assign(height, std::vector<double>(width, 0.0));
		conditional_guides.resize(height);
		texel_probabilities.assign(static_cast<size_t>(width) * height, 0.0);
		total_weight = 0.0;

		if (!image.is_valid() || width <= 0 || height <= 0)
			return;
		
		// Compute the row integrals and conditional CDFs
		for (int y = 0; y < height; y++)
		{
			double row_sum = 0.0;
			for (int x = 0; x < width; x++)
			{
				// The weight of each texel
				const double weight = texel_weight(x, y);
				texel_probabilities[static_cast<size_t>(y) * width + x] = weight;
				row_sum += weight;
				conditional_cdf[y][x] = row_sum; // CDF for x selection in row y
			}

			row_integrals[y] = row_sum;
			total_weight += row_sum;

			if (row_sum > 0.0)
			{
				for(int x = 0; x < width; x++)
					conditional_cdf[y][x] /= row_sum; // Normalize to get the CDF
			}
			else
			{
				// If the row has zero weight, set the CDF to a uniform distribution
				for (int x = 0; x < width; x++)
					conditional_cdf[y][x] = static_cast<double>(x + 1) / static_cast<double>(width);
			}
			conditional_guides[y].build(conditional_cdf[y]);
		}

		// Compute the marginal CDF for row selection
		double accum = 0.0;
		for (int y = 0; y < height; y++)
		{
			accum += row_integrals[y];
			marginal_cdf[y] = accum;
		}

		if (total_weight > 0.0)
		{
			// Preserve the original weight / total calculation and CDF accumulation order.
			for (double& probability : texel_probabilities)
				probability /= total_weight;
			for (int y = 0; y < height; y++)
				marginal_cdf[y] /= total_weight; // Normalize to get the CDF
		}
		else
		{
			// If the entire environment map has zero weight, set the marginal CDF to a uniform distribution
			for (int y = 0; y < height; y++)
				marginal_cdf[y] = static_cast<double>(y + 1) / static_cast<double>(height);
		}
		marginal_guide.build(marginal_cdf);
	}

	// Sample pixel indices (x, y) based on the precomputed sampling distribution
	std::pair<int, int> sample_pixel_indices() const
	{
		if (width <= 0 || height <= 0)
			return { 0, 0 };

		// Choose a random row based on uniform random number
		const double u_row = random_double();
		int y = marginal_guide.lower_bound(marginal_cdf, u_row);
		y = std::clamp(y, 0, height - 1);

		const double u_col = random_double();
		const auto& row_cdf = conditional_cdf[y];
		int x = conditional_guides[y].lower_bound(row_cdf, u_col);
		x = std::clamp(x, 0, width - 1);

		return { x, y };
	}

	// Compute the importance sampling weight for a given pixel (x, y) 
	// based on its luminance and the sine of the elevation angle
	double texel_weight(int x, int y) const
	{
		if (!image.is_valid() || width <= 0 || height <= 0)
			return 0.0;

		x = std::clamp(x, 0, width - 1);
		y = std::clamp(y, 0, height - 1);

		const Color texel = decode_if_needed(image.float_pixel(x, y));

		const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
		const double sin_theta = std::sin(v * pi);

		return luminance(texel) * std::max(sin_theta, 0.0);
	}

	double texel_probability(int x, int y) const
	{
		if (total_weight <= 0.0)
			return 0.0;
		return texel_probabilities[static_cast<size_t>(y) * width + x];
	}
};
