#pragma once
#include <algorithm>
#include <cmath>

#include "My_Common.h"
#include "PDF.h"
#include "Microfacet.h"

struct BSDFSample
{
	Vector3 wi;
	Color f;           // BSDF value (without the cosine); for delta events: the path weight
	double pdf = 0.0;  // solid-angle pdf (0 for delta events)
	bool delta = false;
};

// Scattering function of one path vertex, built once per hit by Material::GetBSDF().
//
// A small value type on the stack: all texture lookups (base color, roughness, metallic, normal
// map) happen once when it is built, and sample / eval / pdf are pure math afterwards. The
// previous design allocated 1-3 PDF objects per bounce with make_shared and re-sampled all
// textures in every Eval() call (Audit P-1, P-3).
//
// Conventions: wo points towards the viewer, wi towards the light; eval() returns f(wo, wi)
// without the cosine term; cosine(wi) is the foreshortening term the integrator multiplies
// with (1 for phase functions).
class BSDF
{
public:
	enum class Kind : unsigned char { Lambert, PBR, Phase, Delta };

	static BSDF lambert(const Vector3& n, const Color& albedo)
	{
		BSDF b;
		b.kind = Kind::Lambert;
		b.n = normalize(n);
		b.albedo = albedo;
		b.cosine_pdf = Cosine_PDF(b.n);
		return b;
	}

	// n: shading normal (normal-mapped, view-corrected); geo_n: geometric normal for sidedness.
	static BSDF pbr(const Vector3& wo, const Vector3& n, const Vector3& geo_n,
		const Color& base_color, double roughness, double metallic)
	{
		BSDF b;
		b.kind = Kind::PBR;
		b.wo = normalize(wo);
		b.n = normalize(n);
		b.geo_n = geo_n;
		b.albedo = base_color;
		b.roughness = roughness;
		b.metallic = metallic;
		b.cosine_pdf = Cosine_PDF(b.n);
		b.ggx_pdf = GGX_PDF(b.n, b.wo, roughness);
		b.p_spec = specular_lobe_probability(b.wo, b.n, base_color, roughness, metallic);
		return b;
	}

	static BSDF phase(const Color& albedo)
	{
		BSDF b;
		b.kind = Kind::Phase;
		b.albedo = albedo;
		b.apply_cosine = false;
		return b;
	}

	// Mirror / glass: the direction is chosen by the material; weight is the throughput factor.
	static BSDF delta(const Vector3& direction, const Color& weight)
	{
		BSDF b;
		b.kind = Kind::Delta;
		b.delta_dir = direction;
		b.albedo = weight;
		return b;
	}

	Kind kind = Kind::Lambert;
	bool is_delta() const { return kind == Kind::Delta; }
	bool applies_cosine() const { return apply_cosine; }
	// Normal used for the cosine term and by eval/pdf (valid for Lambert / PBR).
	const Vector3& shading_normal() const { return n; }
	const Color& base_albedo() const { return albedo; }
	double lobe_roughness() const { return roughness; }
	double lobe_metallic() const { return metallic; }
	double specular_probability() const { return p_spec; }

	double cosine(const Vector3& wi) const
	{
		if (!apply_cosine)
			return 1.0;
		return std::max(dot(n, normalize(wi)), 0.0);
	}

	Color eval(const Vector3& wi_in) const
	{
		switch (kind)
		{
		case Kind::Lambert:
			return dot(n, wi_in) <= 0.0 ? Color(0, 0, 0) : albedo / pi;
		case Kind::Phase:
			return albedo * (1.0 / (4.0 * pi));
		case Kind::PBR:
		{
			const Vector3 l = normalize(wi_in);
			// Sidedness uses the geometric normal (light leaks); shading uses n. Using the
			// interpolated normal here blackens smooth-mesh silhouettes (Audit F8).
			if (dot(geo_n, l) <= 0.0 || dot(geo_n, wo) <= 0.0)
				return Color(0, 0, 0);
			const double n_dot_l = std::max(dot(n, l), 1e-4);
			const double n_dot_v = std::max(dot(n, wo), 1e-4);
			return evaluate_metallic_roughness(n, wo, l, n_dot_v, n_dot_l, albedo, roughness, metallic);
		}
		case Kind::Delta:
			return Color(0, 0, 0);
		}
		return Color(0, 0, 0);
	}

	double pdf(const Vector3& wi) const
	{
		switch (kind)
		{
		case Kind::Lambert:
			return cosine_pdf.value(wi);
		case Kind::Phase:
			return 1.0 / (4.0 * pi);
		case Kind::PBR:
			return (1.0 - p_spec) * cosine_pdf.value(wi) + p_spec * ggx_pdf.value(wi);
		case Kind::Delta:
			return 0.0;
		}
		return 0.0;
	}

	// Returns false for an invalid sample (pdf = 0, e.g. a VNDF reflection below the horizon);
	// the path is terminated, which is the unbiased treatment of that probability mass.
	bool sample(BSDFSample& s) const
	{
		switch (kind)
		{
		case Kind::Delta:
			s.wi = delta_dir;
			s.f = albedo;
			s.pdf = 0.0;
			s.delta = true;
			return true;
		case Kind::Lambert:
			s.wi = cosine_pdf.generate();
			break;
		case Kind::Phase:
			s.wi = Sphere_PDF().generate();
			break;
		case Kind::PBR:
			s.wi = (random_double() < p_spec) ? ggx_pdf.generate() : cosine_pdf.generate();
			break;
		}
		s.delta = false;
		s.pdf = pdf(s.wi);
		if (!(s.pdf > 0.0))
			return false;
		s.f = eval(s.wi);
		return true;
	}

	// Metallic-roughness BRDF (without the cosine term).
	//
	// Specular: Cook-Torrance D * G2 * F / (4 n.v n.l) with the height-correlated Smith G2 of
	// the same GGX model the VNDF sampler uses (ggx::G2), so f * cos / pdf_vndf = F * G2 / G1(v).
	//
	// Diffuse: base / pi * (1 - m) * (1 - E_spec(n.v)) (1 - E_spec(n.l)) / (1 - E_spec_avg),
	// where E_spec is the directional albedo of the specular lobe (ggx::SpecularAlbedoTable).
	// The previous kd = 1 - F(v.h) used the per-microfacet Fresnel for the diffuse layer and
	// reflected more energy than it received at grazing angles (white furnace 1.06). This
	// coupled form is reciprocal, and the diffuse layer only receives the energy the specular
	// lobe does not reflect.
	static Color evaluate_metallic_roughness(
		const Vector3& n, const Vector3& v, const Vector3& l,
		double n_dot_v, double n_dot_l,
		const Color& base_color, double roughness, double metallic)
	{
		const double alpha = roughness * roughness;
		// F0 is 0.04 for dielectric and becomes base color for metals.
		const Color f0 = lerp(Color(0.04, 0.04, 0.04), base_color, metallic);

		const Vector3 h = normalize(v + l);
		const double n_dot_h = dot(n, h);
		const double v_dot_h = std::max(dot(v, h), 0.0);
		const Color F = ggx::fresnel_schlick(v_dot_h, f0);
		const double D = ggx::D(n_dot_h, alpha);
		const double G = ggx::G2(n_dot_v, n_dot_l, alpha);
		const Color specular = F * (D * G / (4.0 * n_dot_v * n_dot_l));

		if (metallic >= 1.0)
			return specular;

		const ggx::SpecularAlbedoTable& table = ggx::SpecularAlbedoTable::get();
		const Color one(1.0, 1.0, 1.0);
		const Color transmit_v = one - table.albedo(n_dot_v, roughness, f0);
		const Color transmit_l = one - table.albedo(n_dot_l, roughness, f0);
		const Color avg = one - table.average(roughness, f0);
		const Color diffuse_norm(std::max(avg.x(), 1e-6), std::max(avg.y(), 1e-6), std::max(avg.z(), 1e-6));
		const Color diffuse = (1.0 - metallic) * base_color * transmit_v * transmit_l / (pi * diffuse_norm);

		return diffuse + specular;
	}

private:
	// Probability of sampling the specular (VNDF) lobe instead of the cosine (diffuse) lobe,
	// proportional to the directional albedo of each lobe at wo (Audit M4). The previous
	// 0.5 + 0.5 m gave 50% of the samples of a dielectric to a lobe carrying ~4% of the energy.
	// Any value in (0, 1) keeps the mixture unbiased; the clamp keeps both lobes sampleable.
	static double specular_lobe_probability(const Vector3& wo, const Vector3& n,
		const Color& base, double roughness, double metallic)
	{
		if (metallic >= 1.0)
			return 1.0;
		const double mu = std::clamp(dot(wo, n), 0.0, 1.0);
		const Color f0 = lerp(Color(0.04, 0.04, 0.04), base, metallic);
		const Color e_spec = ggx::SpecularAlbedoTable::get().albedo(mu, roughness, f0);
		const Color e_diff = (1.0 - metallic) * base * (Color(1, 1, 1) - e_spec);
		auto lum = [](const Color& c) { return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z(); };
		const double s = lum(e_spec);
		const double d = lum(e_diff);
		if (!(s + d > 0.0))
			return 0.5;
		return std::clamp(s / (s + d), 0.1, 0.9);
	}

	Vector3 wo = Vector3(0, 0, 1);
	Vector3 n = Vector3(0, 0, 1);
	Vector3 geo_n = Vector3(0, 0, 1);
	bool apply_cosine = true;
	Color albedo = Color(1, 1, 1);
	double roughness = 1.0;
	double metallic = 0.0;
	double p_spec = 0.0;
	Vector3 delta_dir;
	Cosine_PDF cosine_pdf;
	GGX_PDF ggx_pdf;
};
