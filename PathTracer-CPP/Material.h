#pragma once
#include<algorithm>
#include "Hittable.h"
#include "My_Common.h"
#include "Texture.h"
#include "BSDF.h"

class Material
{
public:
	virtual ~Material() = default;

	// Builds the scattering function at a hit point. Returns false if the path is absorbed
	// (e.g. emitters, or a fuzzy mirror that scatters below the surface). Delta materials
	// (mirror, glass) choose their direction here and return BSDF::delta().
	virtual bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const { return false; }

	// Emitted returns the emitted radiance for a ray hitting the material. Default is no emission
	virtual Color emitted(const Ray& ray_in, const HitRecord& rec, double u, double v, const Point3& p) const { return Color(0, 0, 0); }

	// Returns "How much light is emitted from the material at the given point (u, v, p)"
	virtual double EmissionLuminance(double u, double v, const Point3& p) const
	{
		return 0.0;
	}
	// Returns material's original albedo
	virtual Color Albedo(double u, double v, const Point3& p) const
	{
		return Color(1, 1, 1);
	}
};



class Lambertian : public Material
{
public : 
	Lambertian(const Color& albedo) : tex(std::make_shared<Solid_Color>(albedo)) {}
	Lambertian(std::shared_ptr<Texture> tex) : tex(tex) {}

	bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const override
	{
		bsdf = BSDF::lambert(rec.n, tex->value(rec.u, rec.v, rec.p));
		return true;
	}

	Color Albedo(double u, double v, const Point3& p) const override
	{
		return tex->value(u, v, p);
	}

private : 
	std::shared_ptr<Texture> tex;
};



class Metal : public Material
{
public :
	Metal(const Color& albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

	bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const override
	{
		Vector3 reflected = reflect(ray_in.direction(), rec.n);
		reflected = normalize(reflected) + fuzz * random_unit_vector();
		// A fuzzed reflection below the surface is absorbed, as in the original RTIOW Metal;
		// otherwise the ray continues inside the object (Audit M8).
		if (dot(reflected, rec.n) <= 0.0)
			return false;

		bsdf = BSDF::delta(reflected, albedo);
		return true;
	}
	Color Albedo(double u, double v, const Point3& p) const override
	{
		return albedo;
	}

private :
	Color albedo;
	double fuzz;
};



class Dielectric : public Material
{
public : 
	Dielectric(double ri) : refraction_index(ri) {};

	bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const override
	{
		// The glass surface absorbs nothing
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

		bsdf = BSDF::delta(direction, Color(1.0, 1.0, 1.0));
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

	// Isotropic phase function: f = albedo / (4 pi), no cosine term (rec.n of a medium hit is
	// arbitrary), sampled uniformly over the sphere (Audit F2 / F3).
	bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const override
	{
		bsdf = BSDF::phase(tex->value(rec.u, rec.v, rec.p));
		return true;
	}

	Color Albedo(double u, double v, const Point3& p) const override
	{
		return tex->value(u, v, p);
	}

private :
	std::shared_ptr<Texture> tex;
};



class PBR_Material : public Material
{
public :
	// OpenGL normal maps store +Y in green; DirectX normal maps store -Y
	enum class Normal_Map_Convention
	{
		OpenGL,
		DirectX
	};

	// Metallic-roughness PBR material.
	// Base color is color data, while normal/roughness/metallic are data maps
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

	bool GetBSDF(const Ray& ray_in, const HitRecord& rec, BSDF& bsdf) const override
	{
		// All textures are sampled once per hit; eval / pdf / sample only do math afterwards.
		const double roughness = sample_scalar(roughness_tex, rec, 1.0, 0.05, 1.0);
		const double metallic = sample_scalar(metallic_tex, rec, 0.0, 0.0, 1.0);
		const Color base_color = sample_color(base_tex, rec);
		const Vector3 view_dir = normalize(-ray_in.direction());
		// Eval / pdf / cosine all use the (normal-mapped, view-corrected) shading normal (F4).
		const Vector3 n = corrected_shading_normal(rec, view_dir);
		bsdf = BSDF::pbr(view_dir, n, rec.geo_n, base_color, roughness, metallic);
		return true;
	}

	Color Albedo(double u, double v, const Point3& p) const override
	{
		// base_tex may be null (e.g. OBJ material with PBR maps but Kd = 0); match sample_color().
		return base_tex ? base_tex->value(u, v, p) : Color(1.0, 1.0, 1.0);
	}

private:
	static Color sample_color(const std::shared_ptr<Texture>& tex, const HitRecord& rec)
	{
		return tex ? tex->value(rec.u, rec.v, rec.p) : Color(1.0, 1.0, 1.0);
	}

	// Roughness and metallic maps are scalar textures, so only the red channel is used.
	static double sample_scalar(const std::shared_ptr<Texture>& tex, const HitRecord& rec, double fallback, double min_value, double max_value)
	{
		if (!tex)
			return fallback;

		const Color value = tex->value(rec.u, rec.v, rec.p);
		return std::clamp(value.x(), min_value, max_value);
	}

	// Return a world-space normal from the normal map when valid TBN data exists.
	Vector3 sample_shading_normal(const HitRecord& rec) const
	{
		// If there no normal map or TBN datas
		// Just returns the original normal
		if (!normal_tex || !rec.has_tangent_space)
			return rec.n;
		
		// Subtract the normal data from normal map
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

		// Re-orthogonalize TBN because interpolation and transforms can make it drift.
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
		// Normal maps can tilt below the true surface. Pull them back to avoid black/firefly samples.
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

	std::shared_ptr<Texture> base_tex;
	std::shared_ptr<Texture> normal_tex;
	std::shared_ptr<Texture> roughness_tex;
	std::shared_ptr<Texture> metallic_tex;
	Normal_Map_Convention normal_map_convention;
};
