#pragma once
#include<algorithm>
#include<cmath>
#include "Vector3.h"
#include "Interval.h"

using Color = Vector3;

struct Color_Bytes
{
	unsigned char r = 0;
	unsigned char g = 0;
	unsigned char b = 0;
};

inline Color tone_map_reinhard(const Color& c)
{
	return c / (c + Color(1, 1, 1));
}

inline double linear_to_srgb(double x)
{
	if (x <= 0.0)
		return 0.0;
	if (x <= 0.0031308)
		return 12.92 * x;
	return 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}

inline Color display_transform(const Color& hdr_linear)
{
	Color mapped = tone_map_reinhard(hdr_linear);
	return Color(
		linear_to_srgb(mapped.x()),
		linear_to_srgb(mapped.y()),
		linear_to_srgb(mapped.z())
	);
}

inline Color_Bytes to_color_bytes(const Color& color)
{
	auto r = color.x();
	auto g = color.y();
	auto b = color.z();

	// Replace NaN components with zero.
	if (std::isnan(r)) r = 0.0;
	if (std::isnan(g)) g = 0.0;
	if (std::isnan(b)) b = 0.0;
	r = std::max(r, 0.0);
	g = std::max(g, 0.0);
	b = std::max(b, 0.0);

	// Linear HDR -> display-referred sRGB
	Color display = display_transform(Color(r, g, b));
	r = display.x();
	g = display.y();
	b = display.z();

	// Translate the [0,1] component values to the byte range [0,255].
	static const Interval intensity(0.000, 0.999);
	Color_Bytes bytes;
	bytes.r = static_cast<unsigned char>(256 * intensity.Clamp(r));
	bytes.g = static_cast<unsigned char>(256 * intensity.Clamp(g));
	bytes.b = static_cast<unsigned char>(256 * intensity.Clamp(b));

	return bytes;
}

inline void write_color(std::ostream& out, const Color& color)
{
	Color_Bytes bytes = to_color_bytes(color);

	out << static_cast<int>(bytes.r) << ' '
		<< static_cast<int>(bytes.g) << ' '
		<< static_cast<int>(bytes.b) << '\n';
}

inline Color lerp(const Color& a, const Color& b, double t)
{
	return a * (1.0 - t) + b * t;
}
