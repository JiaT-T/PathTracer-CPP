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
		  srgb_input(srgb_input) {}

	Color radiance(const Vector3& dir) const override
	{
		if (!image.is_valid())
			return Color(1.0, 0.0, 1.0);

		const auto [u, v] = direction_to_uv(dir);
		return sample_bilinear(u, v) * intensity;
	}

private:
	rtw_image image;
	double intensity = 1.0;
	double rotation = 0.0;
	bool srgb_input = false;

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
};
