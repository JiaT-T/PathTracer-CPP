#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "My_Common.h"

// Linear float framebuffer I/O and image comparison metrics.
//
// PFM ("PF" = 3-channel float) stores linear HDR values without tone mapping, which is what
// numerical regression tests need. The 8-bit PPM path (tone map -> sRGB) is kept for preview
// and presentation only; it must never be used for math comparisons.
struct LinearImage
{
	int width = 0;
	int height = 0;
	std::vector<Color> pixels; // row-major, row 0 = top of the image

	LinearImage() = default;
	LinearImage(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h, Color(0, 0, 0)) {}

	Color& at(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
	const Color& at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
	bool empty() const { return pixels.empty(); }
};

namespace image_io
{
	inline bool write_pfm(const std::string& filename, const std::vector<Color>& pixels, int width, int height)
	{
		std::ofstream out(filename, std::ios::binary);
		if (!out.is_open())
		{
			std::cerr << "Error: Cannot open file: " << filename << "\n";
			return false;
		}

		// Negative scale = little-endian. PFM scanlines are stored bottom-to-top.
		out << "PF\n" << width << ' ' << height << "\n-1.0\n";
		std::vector<float> row(static_cast<size_t>(width) * 3);
		for (int y = height - 1; y >= 0; --y)
		{
			for (int x = 0; x < width; ++x)
			{
				const Color& c = pixels[static_cast<size_t>(y) * width + x];
				row[static_cast<size_t>(x) * 3 + 0] = static_cast<float>(c.x());
				row[static_cast<size_t>(x) * 3 + 1] = static_cast<float>(c.y());
				row[static_cast<size_t>(x) * 3 + 2] = static_cast<float>(c.z());
			}
			out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
		}
		return static_cast<bool>(out);
	}

	inline bool write_pfm(const std::string& filename, const LinearImage& image)
	{
		return write_pfm(filename, image.pixels, image.width, image.height);
	}

	inline bool read_pfm(const std::string& filename, LinearImage& image)
	{
		std::ifstream in(filename, std::ios::binary);
		if (!in.is_open())
			return false;

		std::string magic;
		int width = 0;
		int height = 0;
		double scale = 0.0;
		in >> magic >> width >> height >> scale;
		in.get(); // single whitespace after the header
		if (magic != "PF" || width <= 0 || height <= 0 || scale >= 0.0)
		{
			std::cerr << "Error: Unsupported PFM (only little-endian RGB): " << filename << "\n";
			return false;
		}

		image = LinearImage(width, height);
		std::vector<float> row(static_cast<size_t>(width) * 3);
		for (int y = height - 1; y >= 0; --y)
		{
			in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
			if (!in)
				return false;
			for (int x = 0; x < width; ++x)
			{
				image.at(x, y) = Color(
					row[static_cast<size_t>(x) * 3 + 0],
					row[static_cast<size_t>(x) * 3 + 1],
					row[static_cast<size_t>(x) * 3 + 2]);
			}
		}
		return true;
	}

	// ASCII P3 PPM after tone mapping + sRGB OETF (display output).
	inline bool write_ppm(const std::string& filename, const std::vector<Color>& pixels, int width, int height)
	{
		std::ofstream out(filename);
		if (!out.is_open())
		{
			std::cerr << "Error: Cannot open file: " << filename << "\n";
			return false;
		}

		out << "P3\n" << width << ' ' << height << "\n255\n";
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
				write_color(out, pixels[static_cast<size_t>(y) * width + x]);
		return static_cast<bool>(out);
	}

	// Replace the extension of a file name ("a/b.ppm" -> "a/b.pfm"); appends if there is none.
	inline std::string replace_extension(const std::string& filename, const std::string& extension)
	{
		const size_t slash = filename.find_last_of("/\\");
		const size_t dot = filename.find_last_of('.');
		if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
			return filename + extension;
		return filename.substr(0, dot) + extension;
	}

	inline std::string add_suffix(const std::string& filename, const std::string& suffix)
	{
		const size_t slash = filename.find_last_of("/\\");
		const size_t dot = filename.find_last_of('.');
		if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
			return filename + suffix;
		return filename.substr(0, dot) + suffix + filename.substr(dot);
	}
}

// Error metrics computed on linear HDR values (per channel, averaged over all channels).
struct ImageMetrics
{
	double mse = 0.0;
	double rmse = 0.0;
	// relMSE = mean((x - r)^2 / (r^2 + 0.01)); robust for HDR, not dominated by bright pixels.
	double rel_mse = 0.0;
	// PSNR against a peak of 1.0 on linear values (reference only; HDR has no natural peak).
	double psnr = 0.0;
	double mean_test = 0.0;
	double mean_reference = 0.0;
	size_t non_finite = 0;
};

inline ImageMetrics compare_images(const std::vector<Color>& test, const std::vector<Color>& reference)
{
	ImageMetrics m;
	if (test.size() != reference.size() || test.empty())
	{
		m.mse = m.rmse = m.rel_mse = infinity;
		return m;
	}

	double sum_sq = 0.0;
	double sum_rel = 0.0;
	double sum_t = 0.0;
	double sum_r = 0.0;
	size_t count = 0;
	for (size_t i = 0; i < test.size(); ++i)
	{
		for (int c = 0; c < 3; ++c)
		{
			const double t = test[i][c];
			const double r = reference[i][c];
			if (!std::isfinite(t) || !std::isfinite(r))
			{
				m.non_finite++;
				continue;
			}
			const double d = t - r;
			sum_sq += d * d;
			sum_rel += d * d / (r * r + 0.01);
			sum_t += t;
			sum_r += r;
			count++;
		}
	}

	if (count == 0)
		return m;
	m.mse = sum_sq / count;
	m.rmse = std::sqrt(m.mse);
	m.rel_mse = sum_rel / count;
	m.psnr = (m.mse > 0.0) ? 10.0 * std::log10(1.0 / m.mse) : infinity;
	m.mean_test = sum_t / count;
	m.mean_reference = sum_r / count;
	return m;
}

inline double image_mean(const std::vector<Color>& pixels)
{
	if (pixels.empty())
		return 0.0;
	double sum = 0.0;
	for (const Color& c : pixels)
		sum += (c.x() + c.y() + c.z()) / 3.0;
	return sum / static_cast<double>(pixels.size());
}
