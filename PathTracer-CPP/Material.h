#pragma once
#include "Hittable.h"
#include "My_Common.h"
#include "Texture.h"
#include "PDF.h"

class Scattered_Record
{
public :
	Color attenuation;
	std::shared_ptr<PDF> p_pdf;
	bool skip_pdf;
	Ray skip_pdf_ray;
};



class Material
{
public:
	virtual ~Material() = default;

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const { return false; }
	virtual Color emitted(const Ray& ray_in, const HitRecord& rec, double u, double v, const Point3& p) const { return Color(0, 0, 0); }
	virtual double Scattering_PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const { return 0; }
};



class Lambertian : public Material
{
public : 
	Lambertian(const Color& albedo) : tex(std::make_shared<Solid_Color>(albedo)) {}
	Lambertian(std::shared_ptr<Texture> tex) : tex(tex) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		// Create Orthonormal Basis
		ONB uvw(rec.n);
		auto scatter_direcion = uvw.transform(random_cosine_dir());

		//auto scatter_direcion = random_on_hemisphere(rec.n);
		if(scatter_direcion.near_zero())
			scatter_direcion = rec.n;
		s_rec.attenuation = tex->value(rec.u, rec.v, rec.p);
		s_rec.p_pdf = std::make_shared<Cosine_PDF>(rec.n);
		s_rec.skip_pdf = false;
		return true;
	}

	double Scattering_PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		auto cos_theta = dot(rec.n, normalize(scattered.direction()));
		return cos_theta < 0 ? 0 : cos_theta / pi;
		//return 1.0 / (2 * pi);
	}

private : 
	std::shared_ptr<Texture> tex;
};



class Metal : public Material
{
public :
	Metal(const Color& albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		Vector3 reflected = reflect(ray_in.direction(), rec.n);
		reflected = normalize(reflected) + fuzz * random_unit_vector();

		s_rec.attenuation = albedo;
		s_rec.p_pdf = nullptr;
		s_rec.skip_pdf = true;
		s_rec.skip_pdf_ray = Ray(rec.p, reflected, ray_in.time());

		return true;
	}

private :
	Color albedo;
	double fuzz;
};



class Dielectric : public Material
{
public : 
	Dielectric(double ri) : refraction_index(ri) {};

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		// The glass surface absorbs nothing
		s_rec.attenuation = Color(1.0, 1.0, 1.0);
		s_rec.p_pdf = nullptr;
		s_rec.skip_pdf = true;
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

		s_rec.skip_pdf_ray = Ray(rec.p, direction, ray_in.time());
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



class Diffuse_Light : public Material
{
public :
	Diffuse_Light(std::shared_ptr<Texture> tex) : tex(tex) {}
	Diffuse_Light(const Color& emit) : tex(std::make_shared<Solid_Color>(emit)) {}

	Color emitted(const Ray& ray_in, const HitRecord& rec, double u, double v, const Point3& p) const override
	{
		if (!rec.front_face) return Color(0, 0, 0);
		return tex->value(u, v, p);
	}

private :
	std::shared_ptr<Texture> tex;
};



class isotropic : public Material
{
public :
	isotropic(const Color& albedo) : tex(std::make_shared<Solid_Color>(albedo)) {}
	isotropic(std::shared_ptr<Texture> tex) : tex(tex) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		s_rec.attenuation = tex->value(rec.u, rec.v, rec.p);
		s_rec.p_pdf = std::make_shared<Sphere_PDF>();
		s_rec.skip_pdf = false;
		return true;
	}

	double Scattering_PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override { return 1.0 / (4.0 * pi); }

private :
	std::shared_ptr<Texture> tex;
};