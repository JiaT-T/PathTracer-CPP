#pragma once
#include<algorithm>
#include<cmath>
#include<limits>
#include<ostream>
#include "Vector3.h"
#include "Interval.h"

using Color = Vector3;

enum class ToneMapping { Reinhard, AgX };

inline constexpr double kMinExposureEV = -20.0;
inline constexpr double kMaxExposureEV = 20.0;

struct DisplaySettings
{
	double exposure_ev = 0.0;
	ToneMapping tone_mapping = ToneMapping::Reinhard;
};

inline DisplaySettings normalize_display_settings(DisplaySettings settings)
{
	settings.exposure_ev = std::isfinite(settings.exposure_ev)
		? std::clamp(settings.exposure_ev, kMinExposureEV, kMaxExposureEV) : 0.0;
	if (settings.tone_mapping != ToneMapping::AgX)
		settings.tone_mapping = ToneMapping::Reinhard;
	return settings;
}

inline const char* tone_mapping_name(ToneMapping mapping)
{
	return mapping == ToneMapping::AgX ? "AgX" : "Reinhard";
}

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

// Compact AgX approximation adapted from three.js r182 (MIT); see
// THIRD_PARTY_NOTICES.md. This is the base look, not Blender's full OCIO pipeline.
// Both input and output are LINEAR Rec.709/sRGB; display_transform applies the
// output sRGB transfer function once, after this operation.
inline Color tone_map_agx(const Color& linear_rec709)
{
	// Written explicitly as rows: the source GLSL constructors contain columns.
	const Color rec2020(
		0.6274 * linear_rec709.x() + 0.3293 * linear_rec709.y() + 0.0433 * linear_rec709.z(),
		0.0691 * linear_rec709.x() + 0.9195 * linear_rec709.y() + 0.0113 * linear_rec709.z(),
		0.0164 * linear_rec709.x() + 0.0880 * linear_rec709.y() + 0.8956 * linear_rec709.z());
	Color encoded(
		0.856627153315983 * rec2020.x() + 0.0951212405381588 * rec2020.y() + 0.0482516061458583 * rec2020.z(),
		0.137318972929847 * rec2020.x() + 0.761241990602591 * rec2020.y() + 0.101439036467562 * rec2020.z(),
		0.11189821299995 * rec2020.x() + 0.0767994186031903 * rec2020.y() + 0.811302368396859 * rec2020.z());
	constexpr double min_ev = -12.47393;
	constexpr double max_ev = 4.026069;
	for (int channel = 0; channel < 3; ++channel)
	{
		const double log_value = std::log2(std::max(encoded[channel], 1e-10));
		const double x = std::clamp((log_value - min_ev) / (max_ev - min_ev), 0.0, 1.0);
		const double x2 = x * x;
		const double x4 = x2 * x2;
		encoded[channel] = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4
			- 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
	}
	Color linear2020(
		1.1271005818144368 * encoded.x() - 0.11060664309660323 * encoded.y() - 0.016493938717834573 * encoded.z(),
		-0.1413297634984383 * encoded.x() + 1.157823702216272 * encoded.y() - 0.016493938717834257 * encoded.z(),
		-0.14132976349843826 * encoded.x() - 0.11060664309660294 * encoded.y() + 1.2519364065950405 * encoded.z());
	for (int channel = 0; channel < 3; ++channel)
		linear2020[channel] = std::pow(std::max(linear2020[channel], 0.0), 2.2);
	Color result(
		1.6605 * linear2020.x() - 0.5876 * linear2020.y() - 0.0728 * linear2020.z(),
		-0.1246 * linear2020.x() + 1.1329 * linear2020.y() - 0.0083 * linear2020.z(),
		-0.0182 * linear2020.x() - 0.1006 * linear2020.y() + 1.1187 * linear2020.z());
	for (int channel = 0; channel < 3; ++channel)
		result[channel] = std::clamp(result[channel], 0.0, 1.0);
	return result;
}

inline double linear_to_srgb(double x)
{
	if (x <= 0.0)
		return 0.0;
	if (x <= 0.0031308)
		return 12.92 * x;
	return 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}

inline Color display_transform(const Color& hdr_linear, const DisplaySettings& settings = {})
{
	const DisplaySettings normalized = normalize_display_settings(settings);
	const double exposure = std::exp2(normalized.exposure_ev);
	Color exposed;
	for (int channel = 0; channel < 3; ++channel)
	{
		// Keep the existing invalid-sample presentation policy: NaN/Inf -> black.
		const double value = std::isfinite(hdr_linear[channel]) ? std::max(hdr_linear[channel], 0.0) : 0.0;
		// Saturate overflow from finite radiance/exposure to a bright value rather
		// than allowing Reinhard's inf/inf to turn it into NaN (or black).
		const double scaled = value * exposure;
		exposed[channel] = std::isfinite(scaled) ? scaled : std::numeric_limits<double>::max();
	}
	const Color mapped = normalized.tone_mapping == ToneMapping::AgX
		? tone_map_agx(exposed) : tone_map_reinhard(exposed);
	return Color(
		linear_to_srgb(mapped.x()),
		linear_to_srgb(mapped.y()),
		linear_to_srgb(mapped.z())
	);
}

inline Color_Bytes to_color_bytes(const Color& color, const DisplaySettings& settings = {})
{
	// Linear HDR -> display-referred sRGB
	const Color display = display_transform(color, settings);
	const double r = display.x();
	const double g = display.y();
	const double b = display.z();

	// Translate the [0,1] component values to the byte range [0,255].
	static const Interval intensity(0.000, 0.999);
	Color_Bytes bytes;
	bytes.r = static_cast<unsigned char>(256 * intensity.Clamp(r));
	bytes.g = static_cast<unsigned char>(256 * intensity.Clamp(g));
	bytes.b = static_cast<unsigned char>(256 * intensity.Clamp(b));

	return bytes;
}

inline void write_color(std::ostream& out, const Color& color, const DisplaySettings& settings = {})
{
	Color_Bytes bytes = to_color_bytes(color, settings);

	out << static_cast<int>(bytes.r) << ' '
		<< static_cast<int>(bytes.g) << ' '
		<< static_cast<int>(bytes.b) << '\n';
}

inline Color lerp(const Color& a, const Color& b, double t)
{
	return a * (1.0 - t) + b * t;
}
