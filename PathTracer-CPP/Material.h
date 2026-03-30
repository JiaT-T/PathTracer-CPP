#pragma once
#include "Hittable.h"
#include "My_Common.h"

class Material
{
public:
	virtual ~Material() = default;

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Color& attenuation, Ray& scattered) const { return false; }
};

class Lambertian : public Material
{
public : 
	Lambertian(const Color& albedo) : albedo(albedo) {}

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Color& attenuation, Ray& scattered) const override
	{
		auto scatter_direcion = rec.n + random_unit_vector();
		if(scatter_direcion.near_zero())
			scatter_direcion = rec.n;
		scattered = Ray(rec.p, scatter_direcion, ray_in.time());
		attenuation = albedo;
		return true;
	}

private : 
	Color albedo;
};

class Metal : public Material
{
public :
	Metal(const Color& albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Color& attenuation, Ray& scattered) const override
	{
		Vector3 reflected = reflect(ray_in.direction(), rec.n);
		reflected = normalize(reflected) + fuzz * random_unit_vector();
		scattered = Ray(rec.p, reflected, ray_in.time());
		attenuation = albedo;
		return (dot(rec.n, scattered.direction()) > 0);
	}

private :
	Color albedo;
	double fuzz;
};

class Dielectric : public Material
{
public : 
	Dielectric(double ri) : refraction_index(ri) {};

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Color& attenuation, Ray& scattered) const override
	{
		// The glass surface absorbs nothing
		attenuation = Color(1.0, 1.0, 1.0);
		double ri = rec.front_face ? (1.0 / refraction_index) : refraction_index;

		Vector3 unit_direction = normalize(ray_in.direction());
		// Decide whether a ray can be refracted or reflected 
		double cos_theta = std::fmin(dot(-unit_direction, rec.n), 1.0);
		double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);
		Vector3 direction;

		if(1.0 < sin_theta * ri || random_double() < reflrectance(cos_theta, ri))
			direction = reflect(unit_direction, rec.n);
		else
			direction = refract(unit_direction, rec.n, ri);

		scattered = Ray(rec.p, direction, ray_in.time());
		return true;
	}

	static double reflrectance(double cosine, double ref_idx)
	{
		// Use Schlick's approximation for reflectance
		auto r0 = (1 - ref_idx) / (1 + ref_idx);
		r0 = r0 * r0;
		return r0 + (1 - r0) * std::pow((1 - cosine), 5);
	}

private :
	double refraction_index;
};
