#pragma once
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

inline double linear_to_gamma(double linear_component)
{
	if (linear_component > 0)
		return std::sqrt(linear_component);
	return 0;
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

	// Linear Space --> Gamma Space
	r = linear_to_gamma(r);
	g = linear_to_gamma(g);
	b = linear_to_gamma(b);

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
