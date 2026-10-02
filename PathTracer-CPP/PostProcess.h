#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "My_Common.h"
#include "Parallel.h"

// Per-pixel auxiliary data for the denoiser. Accumulated over the same samples as the color
// (jittered sub-pixel positions, depth of field, motion blur), at the first non-delta vertex,
// so mirrors / glass carry the guide of what they show (Audit section 7, items 1 and 4).
struct PixelGuide
{
	Color albedo = Color(1, 1, 1);
	Vector3 normal = Vector3(0, 0, 0);
	double depth = infinity;
	double glossiness = 0.0; // p_spec * (1 - roughness)^2: how much the pixel is a glossy reflection
	double variance = 0.0;   // variance of the pixel mean (luminance)
	double coverage = 0.0;   // fraction of samples that hit geometry
	int sample_count = 0;
	bool valid = false;
	bool emitter = false;    // some samples see an emitter directly (never filtered / mixed)
};

struct PostProcessInput
{
	const std::vector<Color>& color;
	const std::vector<PixelGuide>& guide_buffer;
	int width = 0;
	int height = 0;
};

struct PostProcessOutput
{
	std::vector<Color>& color;
};

class PostProcess
{
public:
	virtual ~PostProcess() = default;

	virtual void Apply(
		const PostProcessInput& input,
		PostProcessOutput& output) const = 0;
};

// Edge-avoiding A-Trous wavelet filter (Dammertz 2010) with SVGF-style variance guidance
// (Schied 2017, spatial part only; there is no temporal accumulation in an offline renderer).
//
// 1. Demodulation (optional): the filter runs on lighting = color / albedo, then multiplies the
//    albedo back, so texture detail is not blurred. Conservative: the albedo is clamped to
//    >= albedo_floor per channel (near-black albedo would amplify noise without bound; since the
//    division is undone exactly afterwards, any positive value keeps the transform invertible),
//    and pixels without a valid guide are passed through untouched. For metals the guide albedo is
//    the base color, i.e. ~F0, so the demodulated signal is the reflected radiance; for glass the
//    guide comes from the surface seen through it.
// 2. Luminance edge-stopping scaled by the standard deviation of the two pixel estimates:
//      w_l = exp(-|l_p - l_q| / (sigma_l * sqrt(Var_p + Var_q) + eps))
//    Noisy regions (high variance) are filtered strongly; converged / high-detail regions (low
//    variance) are left almost untouched, so the filter fades out automatically at high spp.
//    The variance is filtered alongside the color with squared weights, as in SVGF.
// 3. Geometry edge-stopping: normal (power 128) and relative depth, as before.
//
// Settings::legacy() reproduces the previous filter (2 iterations, display-space color sigma
// 0.08, albedo weight, no demodulation / variance) for before/after comparisons.
class AtrousDenoiser : public PostProcess
{
public:
	struct Settings
	{
		bool enabled = true;
		int iterations = 4;
		int thread_count = 0; // 0 = hardware concurrency; Camera shares its render budget.
		double normal_power = 128.0;
		double depth_scale = 0.02;
		// Variance-guided mode
		bool variance_guided = true;
		// Chosen with --denoise-eval --sweep (16 and 64 spp, 4 test scenes): sigma 2 / 4
		// iterations was best or within a few percent of the best everywhere.
		double sigma_luminance = 2.0;
		// sigma is multiplied by (1 - gloss_attenuation * glossiness): mirror-like pixels are
		// filtered much less (Audit section 7, "destroy specular").
		double gloss_attenuation = 0.8;
		bool demodulate_albedo = true;
		double albedo_floor = 0.02;
		// Legacy mode (variance_guided = false)
		double color_sigma = 0.08;
		bool albedo_weight = false;

		static Settings legacy()
		{
			Settings s;
			s.iterations = 2;
			s.variance_guided = false;
			s.demodulate_albedo = false;
			s.albedo_weight = true;
			return s;
		}
	};

	explicit AtrousDenoiser(Settings settings = {}) : settings(settings) {}

	void Apply(
		const PostProcessInput& input,
		PostProcessOutput& output) const override
	{
		const size_t n = input.color.size();
		if (!settings.enabled ||
			settings.iterations <= 0 ||
			n == 0 ||
			n != input.guide_buffer.size() ||
			input.width <= 0 ||
			input.height <= 0 ||
			n != static_cast<size_t>(input.width) * input.height)
		{
			output.color = input.color;
			return;
		}

		// Demodulate.
		std::vector<Color> albedo(n, Color(1, 1, 1));
		std::vector<Color> ping(n);
		std::vector<double> var_ping(n, 0.0);
		for (size_t i = 0; i < n; ++i)
		{
			const PixelGuide& g = input.guide_buffer[i];
			if (settings.demodulate_albedo && g.valid)
			{
				albedo[i] = Color(
					std::max(g.albedo.x(), settings.albedo_floor),
					std::max(g.albedo.y(), settings.albedo_floor),
					std::max(g.albedo.z(), settings.albedo_floor));
			}
			ping[i] = input.color[i] / albedo[i];
			const double a = luminance_of(albedo[i]);
			var_ping[i] = g.variance / (a * a);
		}

		std::vector<Color> pong(n);
		std::vector<double> var_pong(n, 0.0);
		for (int pass = 0; pass < settings.iterations; ++pass)
		{
			const int step = 1 << pass;
			ApplyPass(ping, var_ping, pong, var_pong, input.color, input.guide_buffer, input.width, input.height, step);
			std::swap(ping, pong);
			std::swap(var_ping, var_pong);
		}

		output.color.resize(n);
		for (size_t i = 0; i < n; ++i)
			output.color[i] = ping[i] * albedo[i];
	}

private:
	static constexpr double kernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

	static double luminance_of(const Color& c)
	{
		return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z();
	}

	void ApplyPass(
		const std::vector<Color>& in,
		const std::vector<double>& var_in,
		std::vector<Color>& out,
		std::vector<double>& var_out,
		const std::vector<Color>& reference_color,
		const std::vector<PixelGuide>& guide_buffer,
		int width,
		int height,
		int step) const
	{
		// 3x3 Gaussian prefilter of the variance for a stable edge-stopping scale (SVGF).
		std::vector<double> var_prefiltered(var_in.size(), 0.0);
		if (settings.variance_guided)
		{
			parallel::parallel_for(height, parallel::resolve_thread_count(settings.thread_count), [&](int y, int)
			{
				for (int x = 0; x < width; ++x)
				{
					double vs = 0.0, ws = 0.0;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int sx = x + dx, sy = y + dy;
							if (sx < 0 || sy < 0 || sx >= width || sy >= height)
								continue;
							const double w = (dx == 0 ? 0.5 : 0.25) * (dy == 0 ? 0.5 : 0.25);
							vs += w * var_in[static_cast<size_t>(sy) * width + sx];
							ws += w;
						}
					var_prefiltered[static_cast<size_t>(y) * width + x] = vs / ws;
				}
			});
		}

		// Rows are independent: parallel over rows (the previous filter was single-threaded).
		parallel::parallel_for(height, parallel::resolve_thread_count(settings.thread_count), [&](int y, int)
		{
			for (int x = 0; x < width; ++x)
			{
				const size_t c = static_cast<size_t>(y) * width + x;
				const PixelGuide& center = guide_buffer[c];
				// Directly visible emitters are exact (no noise) and far brighter than their
				// surroundings: never filter them and never use them as neighbors, otherwise
				// the light bleeds into the adjacent surfaces.
				if (!center.valid || (settings.variance_guided && center.emitter))
				{
					out[c] = in[c];
					var_out[c] = var_in[c];
					continue;
				}

				const double l_center = luminance_of(in[c]);
				const double var_center = var_prefiltered[c];

				Color sum(0, 0, 0);
				double var_sum = 0.0;
				double weight_sum = 0.0;
				for (int ky = -2; ky <= 2; ++ky)
				{
					const int sy = y + ky * step;
					if (sy < 0 || sy >= height)
						continue;
					for (int kx = -2; kx <= 2; ++kx)
					{
						const int sx = x + kx * step;
						if (sx < 0 || sx >= width)
							continue;
						const size_t q = static_cast<size_t>(sy) * width + sx;
						const PixelGuide& neighbor = guide_buffer[q];
						if (!neighbor.valid || (settings.variance_guided && neighbor.emitter))
							continue;

						double w = kernel[kx + 2] * kernel[ky + 2] * geometry_weight(center, neighbor);
						if (settings.variance_guided)
						{
							// Symmetric scale sqrt(Var_p + Var_q): w_pq = w_qp, so a noisy bright pixel
							// and its converged dark neighbor treat each other the same way. With the
							// center variance alone, bright outliers averaged their dark neighbors in
							// while the neighbors rejected them, and energy was lost (-4% at 16 spp).
							if (q != c)
							{
								// Glossy reflections carry detail the guides do not describe (the
								// guide stops at the reflecting surface): filter them conservatively.
								const double gloss = std::max(center.glossiness, neighbor.glossiness);
								const double sigma = settings.sigma_luminance * (1.0 - settings.gloss_attenuation * gloss);
								const double scale = sigma *
									std::sqrt(std::max(var_center + var_prefiltered[q], 0.0)) + 1e-10;
								w *= std::exp(-std::abs(l_center - luminance_of(in[q])) / scale);
							}
						}
						else
						{
							w *= legacy_color_weight(reference_color[c], reference_color[q]);
							if (settings.albedo_weight)
								w *= std::exp(-(center.albedo - neighbor.albedo).length_squared() / 0.25);
						}

						sum += w * in[q];
						var_sum += w * w * var_in[q];
						weight_sum += w;
					}
				}

				if (weight_sum > 0.0)
				{
					out[c] = sum / weight_sum;
					var_out[c] = var_sum / (weight_sum * weight_sum);
				}
				else
				{
					out[c] = in[c];
					var_out[c] = var_in[c];
				}
			}
		});
	}

	double geometry_weight(const PixelGuide& center, const PixelGuide& neighbor) const
	{
		const double normal_similarity = std::max(dot(center.normal, neighbor.normal), 0.0);
		const double normal_weight = std::pow(normal_similarity, settings.normal_power);

		const double depth_threshold =
			std::max(1e-6, settings.depth_scale * std::max(1.0, std::abs(center.depth)));
		const double depth_weight =
			std::exp(-std::abs(center.depth - neighbor.depth) / depth_threshold);

		return normal_weight * depth_weight;
	}

	double legacy_color_weight(const Color& center_color, const Color& neighbor_color) const
	{
		const double color_diff = (display_transform(center_color) - display_transform(neighbor_color)).length_squared();
		const double color_sigma2 = std::max(1e-6, settings.color_sigma * settings.color_sigma);
		return std::exp(-color_diff / color_sigma2);
	}

	Settings settings;
};
