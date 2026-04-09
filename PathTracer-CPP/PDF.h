#pragma once
#include<array>

#include "My_Common.h"
#include "ONB.h"

class PDF
{
public :
	virtual ~PDF() {}
	virtual double value(const Vector3& dir) const = 0;
	virtual Vector3 generate() const = 0;
};

class Sphere_PDF : public PDF
{
public :
	Sphere_PDF() {}

	double value(const Vector3& dir) const override
	{
		return 1.0 / (1.0 * pi);
	}
	Vector3 generate() const override
	{
		return random_unit_vector();
	}
};

class Cosine_PDF : public PDF
{
public :
	Cosine_PDF(const Vector3& w) : uvw(w) {};

	double value(const Vector3& dir) const override
	{
		auto cosine_theta = dot(normalize(dir), uvw.w());
		return std::fmax(0, cosine_theta / pi);
	}
	Vector3 generate() const override
	{
		return uvw.transform(random_cosine_dir());
	}

private :
	ONB uvw;
};

class Hittable_PDF : public PDF
{
public :
	Hittable_PDF(const Hittable& object, const Point3& origin) : object(object), origin(origin) {};

	double value(const Vector3& dir) const override
	{
		return object.pdf_value(origin, dir);
	}
	Vector3 generate() const override
	{
		return object.random(origin);
	}

private :
	const Hittable& object;
	Point3 origin;
};

class Mixture_PDF : public PDF
{
public :
	Mixture_PDF(std::shared_ptr<PDF> p1, std::shared_ptr<PDF> p2)
	{
		p[0] = p1;
		p[1] = p2;
	}

	double value(const Vector3& dir) const override
	{
		return 0.5 * p[0]->value(dir) + 0.5 * p[1]->value(dir);
	}
	Vector3 generate() const override
	{
		if (random_double() < 0.5) return p[0]->generate();
		else return p[1]->generate();
	}

private :
	std::array<std::shared_ptr<PDF>, 2> p;
};
