#pragma once
#include<memory>
#include "Color.h"
class Texture
{
public :
	virtual ~Texture() = default;
	virtual Color value(double u, double v, const Point3& p) const = 0;
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