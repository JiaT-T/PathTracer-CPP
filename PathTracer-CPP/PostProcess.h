#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "My_Common.h"

struct PixelGuide
{
	Color albedo = Color(1, 1, 1);
	Vector3 normal = Vector3(0, 0, 0);
	double depth = infinity;
	int sample_count = 0;
	bool valid = false;
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

class AtrousDenoiser : public PostProcess
{
public:
	struct Settings
	{
		bool enabled = true;
		int iterations = 2;
		double normal_power = 128.0;
		double depth_scale = 0.02;
		double color_sigma = 0.08;
	};

	explicit AtrousDenoiser(Settings settings = {}) : settings(settings) {}

	void Apply(
		const PostProcessInput& input,
		PostProcessOutput& output) const override
	{
		if (!settings.enabled ||
			settings.iterations <= 0 ||
			input.color.empty() ||
			input.color.size() != input.guide_buffer.size() ||
			input.width <= 0 ||
			input.height <= 0)
		{
			output.color = input.color;
			return;
		}

		std::vector<Color> ping = input.color;
		std::vector<Color> pong(input.color.size(), Color(0, 0, 0));

		for (int pass = 0; pass < settings.iterations; ++pass)
		{
			const int step = 1 << pass;
			ApplyPass(
				ping,
				pong,
				input.color,
				input.guide_buffer,
				input.width,
				input.height,
				step);
			std::swap(ping, pong);
		}

		output.color = ping;
	}

private:
	static constexpr double kernel[5] =
	{
		1.0 / 16.0,
		4.0 / 16.0,
		6.0 / 16.0,
		4.0 / 16.0,
		1.0 / 16.0
	};

	void ApplyPass(
		const std::vector<Color>& input_color,
		std::vector<Color>& output_color,
		const std::vector<Color>& reference_color,
		const std::vector<PixelGuide>& guide_buffer,
		int width,
		int height,
		int step) const
	{
		output_color.assign(input_color.size(), Color(0, 0, 0));

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const size_t center_index = static_cast<size_t>(y) * width + x;
				const PixelGuide& center = guide_buffer[center_index];

				if (!center.valid)
				{
					output_color[center_index] = input_color[center_index];
					continue;
				}

				Color sum(0, 0, 0);
				double weight_sum = 0.0;

				for (int ky = -2; ky <= 2; ++ky)
				{
					const int sample_y = y + ky * step;
					if (sample_y < 0 || sample_y >= height)
						continue;

					for (int kx = -2; kx <= 2; ++kx)
					{
						const int sample_x = x + kx * step;
						if (sample_x < 0 || sample_x >= width)
							continue;

						const size_t sample_index = static_cast<size_t>(sample_y) * width + sample_x;
						const PixelGuide& neighbor = guide_buffer[sample_index];
						if (!neighbor.valid)
							continue;

						const double spatial_weight = kernel[kx + 2] * kernel[ky + 2];
						const double edge_weight = EdgeWeight(
							center,
							neighbor,
							reference_color[center_index],
							reference_color[sample_index]);
						const double weight = spatial_weight * edge_weight;

						sum += weight * input_color[sample_index];
						weight_sum += weight;
					}
				}

				output_color[center_index] =
					(weight_sum > 0.0) ? (sum / weight_sum) : input_color[center_index];
			}
		}
	}

	double EdgeWeight(
		const PixelGuide& center,
		const PixelGuide& neighbor,
		const Color& center_color,
		const Color& neighbor_color) const
	{
		const Color center_display = display_transform(center_color);
		const Color neighbor_display = display_transform(neighbor_color);
		const double color_diff =
			(center_display - neighbor_display).length_squared();
		const double color_sigma2 =
			std::max(1e-6, settings.color_sigma * settings.color_sigma);
		const double color_weight =
			std::exp(-color_diff / color_sigma2);

		const double albedo_diff =
			(center.albedo - neighbor.albedo).length_squared();
		const double albedo_weight = std::exp(-albedo_diff / 0.25);

		const double normal_similarity =
			std::max(dot(center.normal, neighbor.normal), 0.0);
		const double normal_weight =
			std::pow(normal_similarity, settings.normal_power);

		const double depth_threshold =
			std::max(1e-6, settings.depth_scale * std::max(1.0, std::abs(center.depth)));
		const double depth_weight =
			std::exp(-std::abs(center.depth - neighbor.depth) / depth_threshold);

		return normal_weight * depth_weight * albedo_weight * color_weight;
	}

	Settings settings;
};
