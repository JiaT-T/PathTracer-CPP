#pragma once
#include<algorithm>
#include<array>
#include "ONB.h"
#include "Hittable.h"
#include "Environment.h"
#include "Microfacet.h"
#include "LightSampler.h"

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
		return 1.0 / (4.0 * pi);
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
		// Probability of the mixed strategy must be the same weighted sum used by generate().
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
		// Use the clamped member, not the constructor parameter that shadows it.
		alpha = this->roughness * this->roughness;
	}

	// Specular PDF used by PBR_Material. VNDF samples visible microfacets from the view direction.
	// GGX_NDF : pdf(l) = D(m) * cos_theta / (4 * dot(v, m))
	// GGX_VNDF: pdf(h) = D(m) * G1(v) * dot(v, m) / dot(n, v), then pdf(l) = pdf(h) / (4 * dot(v, m))
	double value(const Vector3& dir) const override
	{
		const Vector3 l = normalize(dir);
		const double n_dot_l = dot(uvw.w(), l);
		if (n_dot_l <= 0.0)
			return 0.0;

		const Vector3 h = normalize(view_dir + l);
		const double n_dot_h = dot(uvw.w(), h);
		const double n_dot_v = std::max(dot(uvw.w(), view_dir), 1e-6);
		return ggx::pdf_vndf_reflection(n_dot_v, n_dot_h, alpha);
	}

	Vector3 generate() const override
	{
		// Sample a visible half vector, then reflect the view vector around it.
		const Vector3 v_local = to_local(view_dir);
		const double u1 = random_double();
		const double u2 = random_double();
		const Vector3 h_local = ggx::sample_vndf_local(v_local, alpha, u1, u2);
		const Vector3 h = normalize(uvw.transform(h_local));
		// A below-horizon reflection is an invalid sample. Return it as-is so value() yields 0
		// and the integrator discards it. Replacing it with the normal would put a point mass
		// at +N that value() does not account for (energy gain on rough / grazing surfaces).
		return reflect(-view_dir, h);
	}

private:
	Vector3 to_local(const Vector3& v) const
	{
		return Vector3(
			dot(v, uvw.u()),
			dot(v, uvw.v()),
			dot(v, uvw.w())
		);
	}

	ONB uvw;
	Vector3 view_dir;
	double roughness;
	double alpha;
};

// Light-selection mixture at one shading point (see LightSampler.h).
class Light_Mixture_PDF : public PDF
{
public:
	Light_Mixture_PDF(const LightSampler& sampler, const Point3& origin, const Vector3* normal)
		: sampler(sampler), origin(origin), probs(sampler.probabilities(origin, normal)) {}

	bool available() const { return probs.any; }

	double value(const Vector3& dir) const override
	{
		return sampler.pdf(probs, origin, dir);
	}

	Vector3 generate() const override
	{
		Vector3 dir(0, 1, 0);
		sampler.sample(probs, origin, dir);
		return dir;
	}

	const LightSampler::Probabilities& probabilities() const { return probs; }

private:
	const LightSampler& sampler;
	Point3 origin;
	LightSampler::Probabilities probs;
};

class Environment_PDF : public PDF
{
public :
	explicit Environment_PDF(const Environment& env) : env(env) {}

	double value(const Vector3& dir) const override
	{
		return env.pdf_value(dir);
	}

	Vector3 generate() const override
	{
		return env.random();
	}

private :
	const Environment& env;
};
