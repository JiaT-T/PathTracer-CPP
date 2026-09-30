#pragma once
#include <algorithm>
#include "ONB.h"
#include "Microfacet.h"

// Direction sampling densities used by the BSDFs. They are small value types (no heap
// allocation, no virtual dispatch): the BSDF of a path vertex holds them by value on the stack.
// All densities are with respect to solid angle.

// Uniform sphere (isotropic phase function).
struct Sphere_PDF
{
	double value(const Vector3&) const
	{
		return 1.0 / (4.0 * pi);
	}
	Vector3 generate() const
	{
		return random_unit_vector();
	}
};

// Cosine-weighted hemisphere around w.
struct Cosine_PDF
{
	Cosine_PDF() = default;
	explicit Cosine_PDF(const Vector3& w) : uvw(w) {}

	double value(const Vector3& dir) const
	{
		auto cosine_theta = dot(normalize(dir), uvw.w());
		return std::fmax(0, cosine_theta / pi);
	}
	Vector3 generate() const
	{
		return uvw.transform(random_cosine_dir());
	}

	ONB uvw;
};

// GGX visible-normal (VNDF) reflection sampling for a fixed outgoing direction.
struct GGX_PDF
{
	GGX_PDF() = default;
	GGX_PDF(const Vector3& normal, const Vector3& view_dir, double roughness)
		: uvw(normal), view_dir(normalize(view_dir)), roughness(std::clamp(roughness, 0.05, 1.0))
	{
		// Use the clamped member, not the constructor parameter that shadows it.
		alpha = this->roughness * this->roughness;
	}

	// pdf(l) = D_v(h) / (4 v.h) = G1(v) D(h) / (4 n.v), see ggx::pdf_vndf_reflection.
	double value(const Vector3& dir) const
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

	Vector3 generate() const
	{
		// Sample a visible half vector, then reflect the view vector around it.
		const Vector3 v_local(dot(view_dir, uvw.u()), dot(view_dir, uvw.v()), dot(view_dir, uvw.w()));
		const double u1 = random_double();
		const double u2 = random_double();
		const Vector3 h_local = ggx::sample_vndf_local(v_local, alpha, u1, u2);
		const Vector3 h = normalize(uvw.transform(h_local));
		// A below-horizon reflection is an invalid sample. Return it as-is so value() yields 0
		// and the integrator discards it. Replacing it with the normal would put a point mass
		// at +N that value() does not account for (energy gain on rough / grazing surfaces).
		return reflect(-view_dir, h);
	}

	ONB uvw;
	Vector3 view_dir = Vector3(0, 0, 1);
	double roughness = 1.0;
	double alpha = 1.0;
};
