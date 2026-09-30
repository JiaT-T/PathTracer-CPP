#pragma once
#include <algorithm>
#include <cmath>

#include "My_Common.h"

// GGX / Trowbridge-Reitz microfacet model shared by the sampler (GGX_PDF) and the BRDF
// evaluation (PBR_Material). Keeping one implementation guarantees that sampling, pdf and
// evaluation describe the same microfacet distribution and the same Smith masking function.
//
// Conventions: alpha = roughness^2, all cosines are with respect to the (shading) normal.
namespace ggx
{
	// Normal distribution D(h).
	inline double D(double n_dot_h, double alpha)
	{
		if (n_dot_h <= 0.0)
			return 0.0;
		const double a2 = alpha * alpha;
		const double d = (n_dot_h * n_dot_h) * (a2 - 1.0) + 1.0;
		return a2 / (pi * d * d);
	}

	// Smith Lambda for GGX: Lambda(w) = (-1 + sqrt(1 + alpha^2 tan^2(theta))) / 2.
	inline double Lambda(double cos_theta, double alpha)
	{
		if (cos_theta <= 0.0)
			return infinity;
		const double c2 = cos_theta * cos_theta;
		const double tan2 = std::max(0.0, 1.0 - c2) / c2;
		return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * tan2));
	}

	// Smith masking G1(w) = 1 / (1 + Lambda(w)). This is the G1 used by the VNDF.
	inline double G1(double cos_theta, double alpha)
	{
		if (cos_theta <= 0.0)
			return 0.0;
		return 1.0 / (1.0 + Lambda(cos_theta, alpha));
	}

	// Height-correlated Smith masking-shadowing G2(v, l) = 1 / (1 + Lambda(v) + Lambda(l))
	// (Heitz 2014). Replaces the Schlick-GGX approximation with Karis' k = (r + 1)^2 / 8,
	// which was designed for analytic point lights and is not the Smith term of this VNDF.
	inline double G2(double n_dot_v, double n_dot_l, double alpha)
	{
		if (n_dot_v <= 0.0 || n_dot_l <= 0.0)
			return 0.0;
		return 1.0 / (1.0 + Lambda(n_dot_v, alpha) + Lambda(n_dot_l, alpha));
	}

	// Pdf of the reflected direction l when h is sampled from the visible normal distribution
	// D_v(h) = G1(v) max(v.h, 0) D(h) / (n.v):  pdf(l) = D_v(h) / (4 v.h) = G1(v) D(h) / (4 n.v).
	inline double pdf_vndf_reflection(double n_dot_v, double n_dot_h, double alpha)
	{
		if (n_dot_v <= 0.0 || n_dot_h <= 0.0)
			return 0.0;
		return G1(n_dot_v, alpha) * D(n_dot_h, alpha) / (4.0 * n_dot_v);
	}

	// Sample a visible microfacet normal in the local frame (z = normal), Heitz 2018.
	inline Vector3 sample_vndf_local(const Vector3& v_local, double alpha, double u1, double u2)
	{
		if (v_local.z() <= 0.0)
			return Vector3(0, 0, 1);

		// Stretch the view vector to the hemisphere configuration.
		Vector3 vh = normalize(Vector3(alpha * v_local.x(), alpha * v_local.y(), v_local.z()));

		const double lensq = vh.x() * vh.x() + vh.y() * vh.y();
		const Vector3 T1 = lensq > 0.0 ? Vector3(-vh.y(), vh.x(), 0) / std::sqrt(lensq) : Vector3(1.0, 0.0, 0.0);
		const Vector3 T2 = cross(vh, T1);

		const double r = std::sqrt(u1);
		const double phi = 2.0 * pi * u2;
		const double t1 = r * std::cos(phi);
		double t2 = r * std::sin(phi);
		const double s = 0.5 * (1.0 + vh.z());
		t2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - t1 * t1)) + s * t2;

		const double z = std::sqrt(std::max(0.0, 1.0 - t1 * t1 - t2 * t2));
		const Vector3 nh = t1 * T1 + t2 * T2 + z * vh;

		// Unstretch.
		return normalize(Vector3(alpha * nh.x(), alpha * nh.y(), std::max(0.0, nh.z())));
	}

	inline Color fresnel_schlick(double cos_theta, const Color& F0)
	{
		const double x = std::clamp(1.0 - cos_theta, 0.0, 1.0);
		const double x2 = x * x;
		const double x5 = x2 * x2 * x;
		return F0 + (Color(1.0, 1.0, 1.0) - F0) * x5;
	}

	// Directional albedo of the single-scattering GGX specular lobe with Schlick Fresnel:
	//   E(mu, r, F0) = integral f_spec(v, l) (n.l) dl = F0 * a(mu, r) + b(mu, r)
	// (Schlick is affine in F0). a and b are tabulated once by stratified VNDF sampling
	// (E = E_vndf[F * G2 / G1(v)]), together with the cosine-weighted averages
	// E_avg(r) = 2 * integral E(mu) mu dmu. Used for the energy-conserving diffuse coupling
	// (Kelemen & Szirmay-Kalos 2001, Kulla & Conty 2017):
	//   f_diffuse = base / pi * (1 - E(mu_v)) (1 - E(mu_l)) / (1 - E_avg)
	class SpecularAlbedoTable
	{
	public:
		static constexpr int kMu = 32;
		static constexpr int kRough = 32;

		static const SpecularAlbedoTable& get()
		{
			static const SpecularAlbedoTable table; // thread-safe one-time construction
			return table;
		}

		Color albedo(double mu, double roughness, const Color& F0) const
		{
			double a = 0.0, b = 0.0;
			lookup(mu, roughness, a, b);
			return F0 * a + Color(b, b, b);
		}

		Color average(double roughness, const Color& F0) const
		{
			const double x = std::clamp(roughness, 0.0, 1.0) * (kRough - 1);
			const int i0 = std::min(static_cast<int>(x), kRough - 2);
			const double t = x - i0;
			const double a = (1.0 - t) * a_avg[i0] + t * a_avg[i0 + 1];
			const double b = (1.0 - t) * b_avg[i0] + t * b_avg[i0 + 1];
			return F0 * a + Color(b, b, b);
		}

	private:
		double a_tab[kRough][kMu] = {};
		double b_tab[kRough][kMu] = {};
		double a_avg[kRough] = {};
		double b_avg[kRough] = {};

		static double mu_at(int j) { return std::max(1e-3, static_cast<double>(j) / (kMu - 1)); }

		SpecularAlbedoTable()
		{
			constexpr int kStrata = 64; // 64 x 64 stratified VNDF samples per entry
			for (int i = 0; i < kRough; ++i)
			{
				const double roughness = static_cast<double>(i) / (kRough - 1);
				const double alpha = std::max(roughness * roughness, 1e-4);
				for (int j = 0; j < kMu; ++j)
				{
					const double mu = mu_at(j);
					const Vector3 v(std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu);
					const double g1_v = G1(mu, alpha);
					double sum_a = 0.0, sum_b = 0.0;
					for (int s1 = 0; s1 < kStrata; ++s1)
					{
						for (int s2 = 0; s2 < kStrata; ++s2)
						{
							const double u1 = (s1 + 0.5) / kStrata;
							const double u2 = (s2 + 0.5) / kStrata;
							const Vector3 h = sample_vndf_local(v, alpha, u1, u2);
							const double v_dot_h = dot(v, h);
							const Vector3 l = 2.0 * v_dot_h * h - v;
							if (l.z() <= 0.0 || v_dot_h <= 0.0)
								continue;
							const double w = G2(mu, l.z(), alpha) / g1_v;
							const double x = 1.0 - v_dot_h;
							const double x5 = x * x * x * x * x;
							sum_a += w * (1.0 - x5);
							sum_b += w * x5;
						}
					}
					a_tab[i][j] = sum_a / (kStrata * kStrata);
					b_tab[i][j] = sum_b / (kStrata * kStrata);
				}

				// 2 * integral E(mu) mu dmu with the trapezoid rule on the tabulated points.
				double acc_a = 0.0, acc_b = 0.0;
				for (int j = 0; j + 1 < kMu; ++j)
				{
					const double m0 = static_cast<double>(j) / (kMu - 1);
					const double m1 = static_cast<double>(j + 1) / (kMu - 1);
					acc_a += 0.5 * (a_tab[i][j] * m0 + a_tab[i][j + 1] * m1) * (m1 - m0);
					acc_b += 0.5 * (b_tab[i][j] * m0 + b_tab[i][j + 1] * m1) * (m1 - m0);
				}
				a_avg[i] = 2.0 * acc_a;
				b_avg[i] = 2.0 * acc_b;
			}
		}

		void lookup(double mu, double roughness, double& a, double& b) const
		{
			const double x = std::clamp(roughness, 0.0, 1.0) * (kRough - 1);
			const double y = std::clamp(mu, 0.0, 1.0) * (kMu - 1);
			const int i0 = std::min(static_cast<int>(x), kRough - 2);
			const int j0 = std::min(static_cast<int>(y), kMu - 2);
			const double tx = x - i0;
			const double ty = y - j0;
			auto bilerp = [&](const double (&t)[kRough][kMu])
			{
				return (1.0 - tx) * ((1.0 - ty) * t[i0][j0] + ty * t[i0][j0 + 1]) +
					tx * ((1.0 - ty) * t[i0 + 1][j0] + ty * t[i0 + 1][j0 + 1]);
			};
			a = bilerp(a_tab);
			b = bilerp(b_tab);
		}
	};
}
