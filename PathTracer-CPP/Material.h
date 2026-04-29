#pragma once
#include<algorithm>
#include "Hittable.h"
#include "My_Common.h"
#include "Texture.h"
#include "PDF.h"

class Scattered_Record
{
public :
	Color attenuation = Color(1.0, 1.0, 1.0);
	std::shared_ptr<PDF> p_pdf;
	bool skip_pdf = false;
	Ray skip_pdf_ray;
};



class Material
{
public:
	virtual ~Material() = default;

	virtual bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const { return false; }
	virtual Color emitted(const Ray& ray_in, const HitRecord& rec, double u, double v, const Point3& p) const { return Color(0, 0, 0); }
	virtual Color Eval(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const { return Color(0, 0, 0); }
	virtual double PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const { return 0; }
};



class Lambertian : public Material
{
public : 
	Lambertian(const Color& albedo) : tex(std::make_shared<Solid_Color>(albedo)) {}
	Lambertian(std::shared_ptr<Texture> tex) : tex(tex) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		s_rec.attenuation = Color(1.0, 1.0, 1.0);
		s_rec.p_pdf = std::make_shared<Cosine_PDF>(rec.n);
		s_rec.skip_pdf = false;
		return true;
	}

	Color Eval(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		auto cos_theta = dot(rec.n, normalize(scattered.direction()));
		if (cos_theta <= 0)
			return Color(0, 0, 0);

		return tex->value(rec.u, rec.v, rec.p) / pi;
	}

	double PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		auto cos_theta = dot(rec.n, normalize(scattered.direction()));
		return cos_theta <= 0 ? 0 : cos_theta / pi;
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
		s_rec.attenuation = Color(1.0, 1.0, 1.0);
		s_rec.p_pdf = std::make_shared<Sphere_PDF>();
		s_rec.skip_pdf = false;
		return true;
	}

	Color Eval(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		return tex->value(rec.u, rec.v, rec.p) * (1.0 / (4.0 * pi));
	}

	double PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		return 1.0 / (4.0 * pi);
	}

private :
	std::shared_ptr<Texture> tex;
};


class GGX_PDF : public PDF
{
public:
	GGX_PDF(const Vector3& normal, const Vector3& view_dir, double roughness)
		: uvw(normal), view_dir(normalize(view_dir)), roughness(std::clamp(roughness, 0.05, 1.0)) {}

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



class PBR_Material : public Material
{
public :
	PBR_Material(std::shared_ptr<Texture> base_tex, std::shared_ptr<Texture> normal_tex, std::shared_ptr<Texture> roughness_tex, std::shared_ptr<Texture> metallic_tex)
		: base_tex(base_tex), normal_tex(normal_tex), roughness_tex(roughness_tex), metallic_tex(metallic_tex) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		const double roughness = sample_scalar(roughness_tex, rec, 1.0, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);
		const double specular_weight = std::clamp(0.5 + 0.5 * metallic, 0.0, 1.0);
		const Vector3 n = sample_shading_normal(rec);

		s_rec.attenuation = Color(1.0, 1.0, 1.0);
		auto diffuse_pdf = std::make_shared<Cosine_PDF>(n);
		auto specular_pdf = std::make_shared<GGX_PDF>(n, normalize(-ray_in.direction()), roughness);

		if (specular_weight <= 0.0)
			s_rec.p_pdf = diffuse_pdf;
		else if (specular_weight >= 1.0)
			s_rec.p_pdf = specular_pdf;
		else
			s_rec.p_pdf = std::make_shared<Mixture_PDF>(diffuse_pdf, specular_pdf);
		s_rec.skip_pdf = false;
		return true;
	}

	Color Eval(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		const Vector3 n = sample_shading_normal(rec);
		const Vector3 v = normalize(-ray_in.direction());
		const Vector3 l = normalize(scattered.direction());
		const double n_dot_l = std::max(dot(n, l), 0.0);
		const double n_dot_v = std::max(dot(n, v), 0.0);
		if (n_dot_l <= 0.0 || n_dot_v <= 0.0)
			return Color(0, 0, 0);

		const Color base_color = sample_color(base_tex, rec);
		const double roughness = sample_scalar(roughness_tex, rec, 1.0, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);
		const Color dielectric_f0(0.04, 0.04, 0.04);
		const Color f0 = lerp(dielectric_f0, base_color, metallic);
		const Color F = fresnel_schlick(std::max(dot(v, normalize(v + l)), 0.0), f0);
		const Color specular = cook_torrance_specular(n, v, l, roughness, f0);
		const Color kd = (Color(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
		const Color diffuse = kd * base_color / pi;

		return diffuse + specular;
	}

	double PDF(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		const double roughness = sample_scalar(roughness_tex, rec, 1.0, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);
		const double specular_weight = std::clamp(0.5 + 0.5 * metallic, 0.0, 1.0);
		const Vector3 n = sample_shading_normal(rec);
		const Cosine_PDF diffuse_pdf(n);
		const GGX_PDF specular_pdf(n, normalize(-ray_in.direction()), roughness);

		if (specular_weight <= 0.0)
			return diffuse_pdf.value(scattered.direction());
		if (specular_weight >= 1.0)
			return specular_pdf.value(scattered.direction());

		return 0.5 * diffuse_pdf.value(scattered.direction()) + 0.5 * specular_pdf.value(scattered.direction());
	}

	Vector3 cook_torrance_specular(
		Vector3 n,
		Vector3 v,
		Vector3 l,
		float roughness,
		Vector3 f0) const
	{
		// half-vector
		Vector3 h = normalize(v + l);

		double n_dot_v = std::max(dot(n, v), 0.0001);
		double n_dot_l = std::max(dot(n, l), 0.0001);
		double n_dot_h = std::max(dot(n, h), 0.0);
		double v_dot_h = std::max(dot(v, h), 0.0);

		double  D = distribution_ggx(n_dot_h, roughness);
		double  G = geometry_smith(n_dot_v, n_dot_l, roughness);
		Vector3 F = fresnel_schlick(v_dot_h, f0);

		Vector3 numerator = D * G * F;
		double denominator = 4.0 * n_dot_v * n_dot_l + 0.0001;

		return numerator / denominator;
	}

private:
	static Color sample_color(const std::shared_ptr<Texture>& tex, const HitRecord& rec)
	{
		return tex ? tex->value(rec.u, rec.v, rec.p) : Color(1.0, 1.0, 1.0);
	}

	static double sample_scalar(const std::shared_ptr<Texture>& tex, const HitRecord& rec, double fallback, double min_value, double max_value)
	{
		if (!tex)
			return fallback;

		const Color value = tex->value(rec.u, rec.v, rec.p);
		return std::clamp(value.x(), min_value, max_value);
	}

	static Color lerp(const Color& a, const Color& b, double t)
	{
		return a * (1.0 - t) + b * t;
	}

	Vector3 fresnel_schlick(double cos_theta, const Vector3& F0) const
	{
		const auto x = std::clamp(1.0 - cos_theta, 0.0, 1.0);
		const auto x2 = x * x;
		const auto x5 = x2 * x2 * x;
		return F0 + (Vector3(1.0, 1.0, 1.0) - F0) * x5;
	}

	double distribution_ggx(double n_dot_h, double roughness) const
	{
		auto a = roughness * roughness;
		auto a2 = a * a;
		auto denom = (n_dot_h * n_dot_h) * (a2 - 1.0) + 1.0;

		return a2 / (pi * denom * denom);
	}

	double geometry_smith(double n_dot_v, double n_dot_l, double roughness) const
	{
		double r = roughness + 1.0;
		double k = (r * r) / 8.0;

		double ggx1 = geometry_schlick_ggx(n_dot_v, k);
		double ggx2 = geometry_schlick_ggx(n_dot_l, k);

		return ggx1 * ggx2;
	}

	double geometry_schlick_ggx(double n_dot_v, double k) const
	{
		double denom = n_dot_v * (1.0 - k) + k;
		return n_dot_v / denom;
	}

	// Returns the World Space normal vector
	Vector3 sample_shading_normal(const HitRecord& rec) const
	{
		if (!normal_tex || !rec.has_tangent_space)
			return rec.n;
		
		Color normal_color = normal_tex->value(rec.u, rec.v, rec.p);
		// Normal in tangent space, remap from [0, 1] to [-1, 1]
		Vector3 n_ts(
			2.0 * normal_color.x() - 1.0,
			2.0 * normal_color.y() - 1.0,
			2.0 * normal_color.z() - 1.0
		);
		n_ts = normalize(n_ts);

		Vector3 N = rec.n;

		Vector3 T = rec.tangent - dot(rec.tangent, N) * N;
		if (T.length_squared() < 1e-10) return rec.n;
		T = normalize(T);

		Vector3 B = cross(N, T);
		if (B.length_squared() < 1e-10) return rec.n;
		B = normalize(B);

		// Normal in world space
		Vector3 n_ws = normalize(
			n_ts.x() * T +
			n_ts.y() * B +
			n_ts.z() * N
		);
		// Ensure the normal is facing the same hemisphere as the geometric normal
		if (dot(n_ws, rec.geo_n) < 0.0)
			n_ws = -n_ws;

		return n_ws;
	}

	std::shared_ptr<Texture> base_tex;
	std::shared_ptr<Texture> normal_tex;
	std::shared_ptr<Texture> roughness_tex;
	std::shared_ptr<Texture> metallic_tex;
};
