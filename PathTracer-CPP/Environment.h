#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "Color.h"
#include "My_Common.h"
#include "rtw_stb_image.h"

class Environment
{
public:
	virtual ~Environment() = default;
	virtual Color radiance(const Vector3& dir) const = 0;
	virtual double pdf_value(const Vector3& dir) const = 0;
	virtual Vector3 random() const = 0;
};

class LatLong_Environment : public Environment
{
public:
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
		if (!image.is_valid())
			return Color(1.0, 0.0, 1.0);

		const auto [u, v] = direction_to_uv(dir);
		return sample_bilinear(u, v) * intensity;
	}

	double pdf_value(const Vector3& dir) const override
	{
		// Degrade to uniform sampling if the environment map is invalid or has no contribution
		if (!image.is_valid() || width <= 0 || height <= 0 || total_weight <= 0.0)
			return 1.0 / (4.0 * pi);

		const Vector3 d = normalize(dir);
		auto [u, v] = direction_to_uv(d);

		int x = static_cast<int>(u * width);
		int y = static_cast<int>(v * height);

		x = std::clamp(x, 0, width - 1);
		y = std::clamp(y, 0, height - 1);

		const double p_texel = texel_probability(x, y);
		
		// Compute the area of the texel in UV space
		const double texel_area_uv = 1.0 / (static_cast<double>(width) * static_cast<double>(height));
		// Convert the texel probability to a PDF value in uv space
		const double pdf_uv = p_texel / texel_area_uv;

		// lat-long: domega = 2*pi*pi*sin(theta) du dv
		const double theta = v * pi;
		const double sin_theta = std::max(std::sin(theta), 1e-6);

		const double pdf_omega = pdf_uv / (2.0 * pi * pi * sin_theta);
		return pdf_omega;
	}

	Vector3 random() const override
	{
		if (!image.is_valid() || width <= 0 || height <= 0 || total_weight <= 0.0)
			return random_unit_vector();

		// Get a random coordinate (x, y) from the precomputed CDFs
		const auto [x, y] = sample_pixel_indices();

		// Jittering the uv in the texel
		const double u = (static_cast<int>(x) + random_double()) / static_cast<double>(width);
		const double v = (static_cast<int>(y) + random_double()) / static_cast<double>(height);

		return uv_to_direction(u, v);
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
	std::vector<double> row_integrals;                 // row_integrals[y]: Integral of the y row (sum of luminance values in the row)
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
	std::pair<double, double> direction_to_uv(const Vector3& dir) const
	{
		const Vector3 d = normalize(dir);

		double phi = std::atan2(d.z(), d.x());
		double theta = std::acos(std::clamp(d.y(), -1.0, 1.0));

		double u = (phi + pi + rotation) / (2.0 * pi);
		double v = theta / pi;

		u = std::fmod(u, 1.0);
		if (u < 0.0)
			u += 1.0;
		v = std::clamp(v, 0.0, 1.0);

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

	// HDR -> LDR
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
	// Transforms the environment map to a datastructure that allows for efficient sampling
	void build_sampling_distribution()
	{
		width = image.width();
		height = image.height();
		
		// Clear and resize the CDF and integral arrays
		row_integrals.assign(height, 0.0);
		marginal_cdf.assign(height, 0.0);
		conditional_cdf.assign(height, std::vector<double>(width, 0.0));
		total_weight = 0.0;

		if (!image.is_valid() || width <= 0 || height <= 0)
			return;
		
		// Compute the row integrals and conditional CDFs
		for (int y = 0; y < height; y++)
		{
			double row_sum = 0.0;
			for (int x = 0; x < width; x++)
			{
				row_sum += texel_weight(x, y);
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
			for (int y = 0; y < height; y++)
				marginal_cdf[y] /= total_weight; // Normalize to get the CDF
		}
		else
		{
			// If the entire environment map has zero weight, set the marginal CDF to a uniform distribution
			for (int y = 0; y < height; y++)
				marginal_cdf[y] = static_cast<double>(y + 1) / static_cast<double>(height);
		}
	}

	// Sample pixel indices (x, y) based on the precomputed sampling distribution
	std::pair<int, int> sample_pixel_indices() const
	{
		if (width <= 0 || height <= 0)
			return { 0, 0 };

		// Choose a random row based on uniform random number
		const double u_row = random_double();
		// Use binary search to find the row index corresponding to u_row in the marginal CDF
		const auto row_it = std::lower_bound(marginal_cdf.begin(), marginal_cdf.end(), u_row);
		// Compute the row index (y) from the iterator
		int y = static_cast<int>(std::distance(marginal_cdf.begin(), row_it));
		y = std::clamp(y, 0, height - 1);

		const double u_col = random_double();
		const auto& row_cdf = conditional_cdf[y];
		const auto col_it = std::lower_bound(row_cdf.begin(), row_cdf.end(), u_col);
		int x = static_cast<int>(std::distance(row_cdf.begin(), col_it));
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
		return texel_weight(x, y) / total_weight;
	}
};