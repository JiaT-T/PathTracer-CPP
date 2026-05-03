#pragma once
#include<memory>
#include "Color.h"
#include "rtw_stb_image.h"
#include "Perlin_Noise.h"

enum class color_space
{
	SRGB,
	Linear
};



class Texture
{
public :
	virtual ~Texture() = default;
	virtual Color value(double u, double v, const Point3& p) const = 0;

	static double srgb_to_linear(double x)
	{
		if (x <= 0.04045)
			return x / 12.92;
		return std::pow((x + 0.055) / 1.055, 2.4);
	}
};



class Solid_Color : public Texture
{
public :
	Solid_Color(const Color& color) : albedo(color) {}
	Solid_Color(double r, double g, double b) : albedo(r, g, b) {}
	Color value(double u, double v, const Point3& p) const override {return albedo; }

private :
	Color albedo;
};



class Checker_Texture : public Texture
{
public :
	Checker_Texture(double scale, std::shared_ptr<Texture> even, std::shared_ptr<Texture> odd) :
		scaleInv(1.0 / scale), even(even), odd(odd) {}
	Checker_Texture(double scale, const Color& c1, const Color& c2) :
		scaleInv(1.0 / scale), even(std::make_shared<Solid_Color>(c1)), odd(std::make_shared<Solid_Color>(c2)) {}

	Color value(double u, double v, const Point3& p) const
	{
		int xInteger = static_cast<int>(std::floor(p.x() * scaleInv));
		int yInteger = static_cast<int>(std::floor(p.y() * scaleInv));
		int zInteger = static_cast<int>(std::floor(p.z() * scaleInv));

		int sum = xInteger + yInteger + zInteger;
		return (sum % 2 == 0) ? even->value(u, v, p) : odd->value(u, v, p);
	}
	
private :
	double scaleInv;
	std::shared_ptr<Texture> even;
	std::shared_ptr<Texture> odd;
};



class Image_Texture : public Texture
{
public :
	Image_Texture(const char* filename) : image(filename) {}
	Image_Texture(const std::string& filename) : image(filename.c_str()) {}
	Image_Texture(const std::string& filename, color_space cs) : image(filename.c_str()), colorSpace(cs) {}
	Color value(double u, double v, const Point3& p) const override
	{
		// If there is no image data, return magenta as a debugging aid.
		if (image.height() <= 0) return Color(1, 0, 1);

		// Clamp input texture coordinates to [0,1] x [1,0]
		u = Interval(0, 1).Clamp(u);
		v = 1.0 - Interval(0, 1).Clamp(v); // Flip V to image coordinates

		auto i = u * image.width();
		auto j = v * image.height();
		auto pixel = image.pixel_data(i, j);

		auto color_scale = 1.0 / 255.0;
		auto r = color_scale * pixel[0];
		auto g = color_scale * pixel[1];
		auto b = color_scale * pixel[2];
		switch (colorSpace)
		{
		case color_space::SRGB:
			return Color(Texture::srgb_to_linear(r), Texture::srgb_to_linear(g), Texture::srgb_to_linear(b));

		case color_space::Linear:
			return Color(r, g, b);
		}

		return Color(r, g, b);
	}

private :
	rtw_image image;
	color_space colorSpace = color_space::SRGB;
};



class Noise_Texture : public Texture
{
public :
	Noise_Texture(double scale) : scale(scale) {}
	Color value(double u, double v, const Point3& p) const override
	{
		return Color(.5, .5, .5) * (1.0 + std::sin(scale * p.z() + 10 * perlin_noise.turb(p, 7)));
	}

private :
	Perlin perlin_noise;
	double scale;
};
