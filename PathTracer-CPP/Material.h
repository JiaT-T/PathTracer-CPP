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
	virtual Vector3 ShadingNormal(const HitRecord& rec) const { return rec.n; }

	// 0.0 : Only prefer light sampling
	// 0.5 : No preference
	// 1.0 : Only prefer BSDF sampling
	virtual double BSDFSamplingPreference(const Ray& ray_in, const HitRecord& rec) const { return 0.5; }

	// Returns "How much light is emitted from the material at the given point (u, v, p)".
	virtual double EmissionLuminance(double u, double v, const Point3& p) const
	{
		return 0.0;
	}
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

	double EmissionLuminance(double u, double v, const Point3& p) const override
	{
		const Color c = tex->value(u, v, p);
		// Returns a constant brightness instead of RGB
		return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z();
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



class PBR_Material : public Material
{
public :
	enum class Normal_Map_Convention
	{
		OpenGL,
		DirectX
	};

	PBR_Material(
		std::shared_ptr<Texture> base_tex,
		std::shared_ptr<Texture> normal_tex,
		std::shared_ptr<Texture> roughness_tex,
		std::shared_ptr<Texture> metallic_tex,
		Normal_Map_Convention normal_map_convention = Normal_Map_Convention::OpenGL)
		: base_tex(base_tex),
		  normal_tex(normal_tex),
		  roughness_tex(roughness_tex),
		  metallic_tex(metallic_tex),
		  normal_map_convention(normal_map_convention) {}

	bool Scatter(const Ray& ray_in, const HitRecord& rec, Scattered_Record& s_rec) const override
	{
		const double roughness = sample_scalar(roughness_tex, rec, 1.0, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);
		const double specular_weight = compute_specular_weight(metallic);
		const double diffuse_weight = 1.0 - specular_weight;
		const Vector3 view_dir = normalize(-ray_in.direction());
		const Vector3 n = corrected_shading_normal(rec, view_dir);

		s_rec.attenuation = Color(1.0, 1.0, 1.0);
		auto diffuse_pdf = std::make_shared<Cosine_PDF>(n);
		auto specular_pdf = std::make_shared<GGX_PDF>(n, view_dir, roughness);

		s_rec.p_pdf = std::make_shared<Mixture_PDF>(diffuse_pdf, specular_pdf, diffuse_weight);
		s_rec.skip_pdf = false;
		return true;
	}

	Color Eval(const Ray& ray_in, const HitRecord& rec, const Ray& scattered) const override
	{
		const Vector3 v = normalize(-ray_in.direction());
		const Vector3 l = normalize(scattered.direction());
		const Vector3 n = corrected_shading_normal(rec, v);
		const double g_dot_l = std::max(dot(rec.n, l), 0.0);
		const double g_dot_v = std::max(dot(rec.n, v), 0.0);
		if (g_dot_l <= 0.0 || g_dot_v <= 0.0)
			return Color(0, 0, 0);
		const double n_dot_l = std::max(dot(n, l), 1e-4);
		const double n_dot_v = std::max(dot(n, v), 1e-4);

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
		const double specular_weight = compute_specular_weight(metallic);
		const double diffuse_weight = 1.0 - specular_weight;
		const Vector3 n = corrected_shading_normal(rec, normalize(-ray_in.direction()));
		const Cosine_PDF diffuse_pdf(n);
		const GGX_PDF specular_pdf(n, normalize(-ray_in.direction()), roughness);

		return diffuse_weight * diffuse_pdf.value(scattered.direction()) + 
			   specular_weight * specular_pdf.value(scattered.direction());
	}

	Vector3 ShadingNormal(const HitRecord& rec) const override
	{
		return sample_shading_normal(rec);
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

	double BSDFSamplingPreference(const Ray& ray_in, const HitRecord& rec) const override
	{
		const double roughness = sample_scalar(roughness_tex, rec, 0.5, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);

		const double specular_weight = compute_specular_weight(metallic);
		const double gloss_factor = 1.0 - roughness;

		// Not a strict formula
		// but a heuristic which thinks metals and glossy surfaces should prefer BSDF sampling more than diffuse surfaces
		const double preference = 0.15 + 0.55 * specular_weight + 0.20 * gloss_factor;

		return std::clamp(preference, 0.1, 0.9);
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
		if (normal_map_convention == Normal_Map_Convention::DirectX)
			n_ts[1] = -n_ts[1];
		n_ts = normalize(n_ts);

		Vector3 N = rec.n;

		Vector3 T = rec.tangent - dot(rec.tangent, N) * N;
		if (T.length_squared() < 1e-10) return rec.n;
		T = normalize(T);

		Vector3 B = rec.bitangent - dot(rec.bitangent, N) * N - dot(rec.bitangent, T) * T;
		if (B.length_squared() < 1e-10)
		{
			const double handedness = dot(cross(N, T), rec.bitangent) < 0.0 ? -1.0 : 1.0;
			B = handedness * cross(N, T);
		}
		if (B.length_squared() < 1e-10) return rec.n;
		B = normalize(B);
		if (dot(cross(N, T), B) < 0.0)
			B = -B;

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

	static Vector3 correct_shading_normal_to_direction(const Vector3& shading_normal, const Vector3& geometric_normal, const Vector3& direction)
	{
		const double geom_dot = dot(geometric_normal, direction);
		const double shade_dot = dot(shading_normal, direction);
		if (geom_dot <= 0.0 || shade_dot > 0.0)
			return shading_normal;

		const double t = std::clamp(geom_dot / (geom_dot - shade_dot + 1e-6), 0.0, 1.0);
		Vector3 corrected = normalize((1.0 - 0.999 * t) * geometric_normal + (0.999 * t) * shading_normal);
		if (dot(corrected, geometric_normal) < 0.0)
			corrected = -corrected;
		return corrected;
	}

	Vector3 corrected_shading_normal(const HitRecord& rec, const Vector3& view_dir) const
	{
		Vector3 n = sample_shading_normal(rec);
		n = correct_shading_normal_to_direction(n, rec.geo_n, view_dir);
		return n;
	}

	static double compute_specular_weight(double metallic)
	{
		return std::clamp(0.5 + 0.5 * metallic, 0.0, 1.0);
	}

	std::shared_ptr<Texture> base_tex;
	std::shared_ptr<Texture> normal_tex;
	std::shared_ptr<Texture> roughness_tex;
	std::shared_ptr<Texture> metallic_tex;
	Normal_Map_Convention normal_map_convention;
};
