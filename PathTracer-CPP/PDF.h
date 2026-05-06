#pragma once
#include<algorithm>
#include<array>
#include "ONB.h"
#include "Hittable.h"

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
		: Mixture_PDF(p1, p2, 0.5) {}

	Mixture_PDF(std::shared_ptr<PDF> p1, std::shared_ptr<PDF> p2, double weight0)
	{
		p[0] = p1;
		p[1] = p2;
		this->weight0 = std::clamp(weight0, 0.0, 1.0);
	}

	double value(const Vector3& dir) const override
	{
		return weight0 * p[0]->value(dir) + (1.0 - weight0) * p[1]->value(dir);
	}
	Vector3 generate() const override
	{
		if (random_double() < weight0) return p[0]->generate();
		else return p[1]->generate();
	}

private :
	std::array<std::shared_ptr<PDF>, 2> p;
	double weight0;
};

class GGX_PDF : public PDF
{
public:
	GGX_PDF(const Vector3& normal, const Vector3& view_dir, double roughness)
		: uvw(normal), view_dir(normalize(view_dir)), roughness(std::clamp(roughness, 0.05, 1.0)) {
	}

	double value(const Vector3& dir) const override
	{
		const Vector3 l = normalize(dir);
		const double n_dot_l = dot(uvw.w(), l);
		if (n_dot_l <= 0.0)
			return 0.0;

		const Vector3 h = normalize(view_dir + l);
		const double n_dot_h = std::max(dot(uvw.w(), h), 0.0);
		const double v_dot_h = std::max(dot(view_dir, h), 1e-6);
		if (n_dot_h <= 0.0)
			return 0.0;

		const double alpha = roughness * roughness;
		const double alpha2 = alpha * alpha;
		const double denom = (n_dot_h * n_dot_h) * (alpha2 - 1.0) + 1.0;
		const double D = alpha2 / (pi * denom * denom);

		return (D * n_dot_h) / (4.0 * v_dot_h);
	}

	Vector3 generate() const override
	{
		const Vector3 half_vector = sample_half_vector();
		Vector3 reflected = reflect(-view_dir, half_vector);
		if (dot(reflected, uvw.w()) <= 0.0)
			reflected = uvw.w();
		return normalize(reflected);
	}

private:
	Vector3 sample_half_vector() const
	{
		const double u1 = random_double();
		const double u2 = random_double();
		const double alpha = roughness * roughness;
		const double alpha2 = alpha * alpha;
		const double phi = 2.0 * pi * u1;
		const double cos_theta = std::sqrt((1.0 - u2) / (1.0 + (alpha2 - 1.0) * u2));
		const double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));

		const Vector3 local_half(
			std::cos(phi) * sin_theta,
			std::sin(phi) * sin_theta,
			cos_theta);

		return normalize(uvw.transform(local_half));
	}

	ONB uvw;
	Vector3 view_dir;
	double roughness;
};
