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
		: uvw(normal), view_dir(normalize(view_dir)), roughness(std::clamp(roughness, 0.05, 1.0)) 
	{
		alpha = roughness * roughness;
	}

	// GGX_NDF : pdf(l) = D(m) * cos_theta / (4 * dot(v, m))
	// GGX_VNDF: pdf(h) = D(m) * G1(v) * dot(v, m) / dot(n, v), then pdf(l) = pdf(h) / (4 * dot(v, m))
	double value(const Vector3& dir) const override
	{
		const Vector3 l = normalize(dir);
		const double n_dot_l = dot(uvw.w(), l);
		if (n_dot_l <= 0.0)
			return 0.0;

		const Vector3 h = normalize(view_dir + l);
		const double n_dot_h = std::max(dot(uvw.w(), h), 0.0);
		const double n_dot_v = std::max(dot(uvw.w(), view_dir), 1e-6);
		const double v_dot_h = std::max(dot(view_dir, h), 1e-6);
		if (n_dot_h <= 0.0)
			return 0.0;

		const double D = GGX_D(n_dot_h);
		const double smith_G1 = Smith_G(n_dot_v);

		const double pdf_h = D * smith_G1 * v_dot_h / n_dot_v;

		return pdf_h / (4.0 * v_dot_h);
	}

	Vector3 generate() const override
	{
		const Vector3 v_local = to_local(view_dir);
		const Vector3 h_local = sample_visible_half_vector_local(v_local);
		const Vector3 h = normalize(uvw.transform(h_local));
		const Vector3 l = reflect(-view_dir, h);
		if (dot(l, uvw.w()) <= 0.0)
			return uvw.w();
		return l;
	}

private:
	// NDF sampling method for GGX distribution
	Vector3 sample_half_vector() const
	{
		const double u1 = random_double();
		const double u2 = random_double();
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

	// VNDF sampling method for GGX distribution
	Vector3 sample_visible_half_vector_local(const Vector3& v_local) const
	{
		if (v_local.z() <= 0)
			return Vector3(0, 0, 1);

		// Stretch view direction to roughness space
		Vector3 vh = Vector3(
			v_local.x() * alpha,
			v_local.y() * alpha,
			v_local.z());
		vh = normalize(vh);

		// Create based vector with stretched view direction
		double lensq = vh.x() * vh.x() + vh.y() * vh.y();
		Vector3 T1 = lensq > 0.0 
			? Vector3(-vh.y(), vh.x(), 0) / std::sqrt(lensq)
			: Vector3(1.0, 0.0, 0.0);
		Vector3 T2 = cross(vh, T1);

		// Sample point with polar coordinates (r, phi) in "Visibily Nomal Domain"
		double u1 = random_double();
		double u2 = random_double();

		double r = std::sqrt(u1);
		double phi = 2.0 * pi * u2;

		double t1 = r * std::cos(phi);
		double t2 = r * std::sin(phi);

		// Reproject on hemisphere
		// which can make sample direction more likely align with those nomal directions that are visible to the view direction
		// rather than sampling uniformly in the hemisphere which can generate many invisible samples when roughness is small
		double s = 0.5 * (1.0 + vh.z());
		t2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - t1 * t1)) + s * t2;

		double z = std::sqrt(std::max(0.0, 1.0 - t1 * t1 - t2 * t2));
		Vector3 nh = t1 * T1 + t2 * T2 + z * vh;

		Vector3 h = normalize(Vector3(alpha * nh.x(), alpha * nh.y(), std::max(0.0, nh.z())));

		return h;
	}

	Vector3 to_local(const Vector3& v) const
	{
		return Vector3(
			dot(v, uvw.u()),
			dot(v, uvw.v()),
			dot(v, uvw.w())
		);
	}

	// D = alpha^2 / (pi * ((n_dot_h^2) * (alpha^2 - 1) + 1)^2)
	double GGX_D(double n_dot_h) const
	{
		const double alpha2 = alpha * alpha;
		const double denom = (n_dot_h * n_dot_h) * (alpha2 - 1.0) + 1.0;
		const double D = alpha2 / (pi * denom * denom);
		return D;
	}

	// G = G1(v) * G1(l)
	// G1(x) = 2 / (1 + sqrt(1 + alpha^2 * tan^2(theta_x)))
	double Smith_G(double n_dot_v) const
	{
		if(n_dot_v <= 0.0)
			return 0.0;
		const double alpha2 = alpha * alpha;
		const double sin2 = std::max(0.0, 1.0 - n_dot_v * n_dot_v);
		const double tan2 = sin2 / std::max(n_dot_v * n_dot_v, 1e-6);
		return 2.0 / (1.0 + std::sqrt(1.0 + alpha2 * tan2));
	}

	ONB uvw;
	Vector3 view_dir;
	double roughness;
	double alpha;
};
