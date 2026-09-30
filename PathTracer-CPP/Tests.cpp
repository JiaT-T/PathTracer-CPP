#include "Tests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "My_Common.h"
#include "ONB.h"
#include "PDF.h"
#include "Material.h"
#include "Environment.h"
#include "Sphere.h"
#include "Quad.h"
#include "Texture.h"
#include "ImageIO.h"
#include "Scenes.h"
#include "Parallel.h"
#include "LightSampler.h"
#include "Hittable_List.h"
#include "Integrator.h"
#include "Stats.h"

// ============================================================================================
// Minimal test harness
// ============================================================================================
namespace
{
	struct TestState
	{
		int checks = 0;
		int failures = 0;
		std::vector<std::string> failed;
	};
	TestState g_state;

	bool check(bool ok, const std::string& name, const std::string& detail = "")
	{
		g_state.checks++;
		if (!ok)
		{
			g_state.failures++;
			g_state.failed.push_back(name);
		}
		std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name;
		if (!detail.empty())
			std::cout << "  " << detail;
		std::cout << "\n" << std::flush;
		return ok;
	}

	std::string fmt(double v, int precision = 5)
	{
		std::ostringstream s;
		s << std::fixed << std::setprecision(precision) << v;
		return s.str();
	}

	// Numerically integrate g(w) over the full sphere on a grid centred on `axis`.
	// theta' = pi * t^2 concentrates cells near the axis, so peaked lobes (GGX with
	// roughness 0.05, i.e. alpha = 0.0025) are resolved while the rest of the sphere is covered.
	template <typename G>
	double integrate_sphere_centered(const Vector3& axis, int n_theta, int n_phi, G&& g)
	{
		const ONB frame(axis);
		double sum = 0.0;
		const double d_phi = 2.0 * pi / n_phi;
		for (int i = 0; i < n_theta; ++i)
		{
			const double t = (i + 0.5) / n_theta;
			const double theta = pi * t * t;
			const double d_theta = 2.0 * pi * t / n_theta;
			const double sin_theta = std::sin(theta);
			const double cos_theta = std::cos(theta);
			double row = 0.0;
			for (int j = 0; j < n_phi; ++j)
			{
				const double phi = (j + 0.5) * d_phi;
				const Vector3 w = frame.transform(Vector3(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta));
				row += g(w);
			}
			sum += row * sin_theta * d_theta * d_phi;
		}
		return sum;
	}

	// Wilson-Hilferty approximation of the chi-square upper tail probability.
	double chi2_p_value(double chi2, int dof)
	{
		if (dof <= 0)
			return 1.0;
		const double k = static_cast<double>(dof);
		const double z = (std::cbrt(chi2 / k) - (1.0 - 2.0 / (9.0 * k))) / std::sqrt(2.0 / (9.0 * k));
		return 0.5 * std::erfc(z / std::sqrt(2.0));
	}

	struct Chi2Result
	{
		double chi2 = 0.0;
		int dof = 0;
		double p_value = 0.0;
		double invalid_observed = 0.0;
		double invalid_expected = 0.0;
	};

	// Chi-square goodness-of-fit between a direction sampler and its claimed pdf.
	// Bins are uniform in (z = cos(theta), phi) around `normal`, i.e. equal solid angle.
	// sample() returns false for an invalid sample (e.g. a VNDF reflection below the horizon);
	// the expected count of invalid samples is N * (1 - integral of pdf), so probability mass
	// lost by the sampler but not by the pdf (or vice versa) is detected.
	template <typename SampleFn, typename PdfFn>
	Chi2Result chi2_test(const Vector3& normal, SampleFn&& sample, PdfFn&& pdf, int sample_count,
		int z_bins = 32, int phi_bins = 64, int sub = 16)
	{
		const ONB frame(normal);
		const int bin_count = z_bins * phi_bins;
		std::vector<double> expected(bin_count, 0.0);
		std::vector<double> observed(bin_count, 0.0);

		const double dz = 2.0 / z_bins;
		const double dphi = 2.0 * pi / phi_bins;
		double total_expected = 0.0;
		for (int zi = 0; zi < z_bins; ++zi)
		{
			for (int pi_ = 0; pi_ < phi_bins; ++pi_)
			{
				double integral = 0.0;
				for (int a = 0; a < sub; ++a)
				{
					const double z = -1.0 + (zi + (a + 0.5) / sub) * dz;
					const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
					for (int b = 0; b < sub; ++b)
					{
						const double phi = (pi_ + (b + 0.5) / sub) * dphi;
						const Vector3 w = frame.transform(Vector3(r * std::cos(phi), r * std::sin(phi), z));
						integral += pdf(w);
					}
				}
				integral *= (dz * dphi) / (sub * sub);
				expected[zi * phi_bins + pi_] = integral * sample_count;
				total_expected += integral;
			}
		}

		Chi2Result result;
		for (int s = 0; s < sample_count; ++s)
		{
			Vector3 w;
			if (!sample(w))
			{
				result.invalid_observed += 1.0;
				continue;
			}
			w = normalize(w);
			const double z = std::clamp(dot(w, frame.w()), -1.0, 1.0);
			double phi = std::atan2(dot(w, frame.v()), dot(w, frame.u()));
			if (phi < 0.0)
				phi += 2.0 * pi;
			const int zi = std::clamp(static_cast<int>((z + 1.0) / dz), 0, z_bins - 1);
			const int pi_ = std::clamp(static_cast<int>(phi / dphi), 0, phi_bins - 1);
			observed[zi * phi_bins + pi_] += 1.0;
		}
		result.invalid_expected = std::max(0.0, (1.0 - total_expected) * sample_count);

		// Pool low-expectation bins so the chi-square approximation holds.
		double pooled_e = 0.0;
		double pooled_o = 0.0;
		int cells = 0;
		auto add_cell = [&](double o, double e)
		{
			result.chi2 += (o - e) * (o - e) / e;
			cells++;
		};
		for (int b = 0; b < bin_count; ++b)
		{
			if (expected[b] < 5.0)
			{
				pooled_e += expected[b];
				pooled_o += observed[b];
			}
			else
				add_cell(observed[b], expected[b]);
		}
		if (result.invalid_expected >= 5.0)
			add_cell(result.invalid_observed, result.invalid_expected);
		else
		{
			pooled_e += result.invalid_expected;
			pooled_o += result.invalid_observed;
		}
		if (pooled_e >= 5.0)
			add_cell(pooled_o, pooled_e);
		else if (pooled_o > 20.0)
			result.chi2 += 1e9; // mass where the pdf claims (almost) none

		result.dof = cells - 1;
		result.p_value = chi2_p_value(result.chi2, result.dof);
		return result;
	}
}

// ============================================================================================
// BSDF adapter
//
// All BSDF tests go through this small interface (sample / eval / pdf for a given surface and
// outgoing direction) so they test the exact code path the integrator uses.
// ============================================================================================
namespace
{
	struct SurfaceQuery
	{
		std::shared_ptr<Material> material;
		HitRecord rec;
		Ray ray_in;
		Vector3 wo; // towards the viewer
	};

	SurfaceQuery make_query(std::shared_ptr<Material> material, const Vector3& normal, const Vector3& wo)
	{
		SurfaceQuery q;
		q.material = std::move(material);
		q.wo = normalize(wo);
		q.rec.p = Point3(0, 0, 0);
		q.rec.n = normalize(normal);
		q.rec.geo_n = q.rec.n;
		q.rec.front_face = true;
		q.rec.u = 0.5;
		q.rec.v = 0.5;
		q.rec.t = 1.0;
		q.rec.mat = q.material.get();
		q.ray_in = Ray(q.rec.p + q.wo, -q.wo, 0.0);
		return q;
	}

	struct BsdfSampleResult
	{
		bool valid = false;
		bool delta = false;
		Vector3 wi;
		Color f;          // BSDF value (without cosine); for delta: the path weight
		double pdf = 0.0; // solid-angle pdf (0 for invalid samples)
		double cos = 0.0; // foreshortening term used by the integrator
	};

	// Wraps the BSDF the integrator builds for one surface query (Material::GetBSDF).
	class BsdfUnderTest
	{
	public:
		explicit BsdfUnderTest(const SurfaceQuery& q) : q(q)
		{
			scatters = q.material->GetBSDF(q.ray_in, q.rec, bsdf);
		}

		Color eval(const Vector3& wi) const { return scatters ? bsdf.eval(wi) : Color(0, 0, 0); }
		double cosine(const Vector3& wi) const { return bsdf.cosine(wi); }
		double pdf(const Vector3& wi) const { return scatters ? bsdf.pdf(wi) : 0.0; }

		BsdfSampleResult sample() const
		{
			BsdfSampleResult r;
			if (!scatters)
				return r;
			BSDFSample s;
			if (!bsdf.sample(s))
				return r;
			r.valid = true;
			r.delta = s.delta;
			r.wi = normalize(s.wi);
			r.f = s.f;
			r.pdf = s.pdf;
			r.cos = s.delta ? 1.0 : bsdf.cosine(s.wi);
			return r;
		}

		bool applies_cosine() const { return bsdf.applies_cosine(); }
		const BSDF& get() const { return bsdf; }

	private:
		SurfaceQuery q;
		BSDF bsdf;
		bool scatters = false;
	};

	struct AlbedoEstimate
	{
		double mean = 0.0;
		double sigma = 0.0;   // standard error of the mean
		double invalid_fraction = 0.0;
	};

	// E[f * cos / pdf] by importance sampling (luminance of the throughput weight).
	AlbedoEstimate estimate_albedo_sampling(const BsdfUnderTest& bsdf, int samples)
	{
		double sum = 0.0;
		double sum2 = 0.0;
		int invalid = 0;
		for (int s = 0; s < samples; ++s)
		{
			const BsdfSampleResult r = bsdf.sample();
			double w = 0.0;
			if (r.valid && !r.delta)
			{
				const Color c = r.f * (r.cos / r.pdf);
				w = (c.x() + c.y() + c.z()) / 3.0;
			}
			else if (!r.valid)
				invalid++;
			sum += w;
			sum2 += w * w;
		}
		AlbedoEstimate e;
		e.mean = sum / samples;
		const double var = std::max(0.0, sum2 / samples - e.mean * e.mean);
		e.sigma = std::sqrt(var / samples);
		e.invalid_fraction = static_cast<double>(invalid) / samples;
		return e;
	}

	// Integral of f * cos over the sphere by deterministic quadrature (independent of sampling).
	double albedo_quadrature(const BsdfUnderTest& bsdf, const Vector3& axis)
	{
		return integrate_sphere_centered(axis, 1600, 384, [&](const Vector3& wi)
		{
			const Color c = bsdf.eval(wi) * bsdf.cosine(wi);
			return (c.x() + c.y() + c.z()) / 3.0;
		});
	}

	std::shared_ptr<PBR_Material> solid_pbr(const Color& base, double roughness, double metallic)
	{
		return std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(base),
			nullptr,
			std::make_shared<Solid_Color>(roughness, roughness, roughness),
			std::make_shared<Solid_Color>(metallic, metallic, metallic));
	}

	Vector3 view_dir_for_cos(double cos_theta)
	{
		const double s = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
		return Vector3(s, 0.0, cos_theta);
	}

	// Independent reference: exact single-scattering albedo of a white (F = 1) GGX conductor
	// with the height-correlated Smith masking-shadowing term, by quadrature.
	double reference_metal_albedo(double roughness, double cos_v)
	{
		const double alpha = roughness * roughness;
		const double a2 = alpha * alpha;
		const Vector3 n(0, 0, 1);
		const Vector3 v = view_dir_for_cos(cos_v);
		auto lambda = [&](double mu)
		{
			const double mu2 = mu * mu;
			return 0.5 * (-1.0 + std::sqrt(1.0 + a2 * (1.0 - mu2) / mu2));
		};
		const Vector3 mirror = reflect(-v, n);
		return integrate_sphere_centered(mirror, 2400, 512, [&](const Vector3& l)
		{
			const double nl = dot(n, l);
			if (nl <= 0.0)
				return 0.0;
			const Vector3 h = normalize(v + l);
			const double nh = dot(n, h);
			if (nh <= 0.0)
				return 0.0;
			const double d = (nh * nh) * (a2 - 1.0) + 1.0;
			const double D = a2 / (pi * d * d);
			const double G2 = 1.0 / (1.0 + lambda(cos_v) + lambda(nl));
			return D * G2 / (4.0 * cos_v * nl) * nl;
		});
	}
}

// ============================================================================================
// Tests
// ============================================================================================
namespace
{
	void test_rng()
	{
		std::cout << "\n[rng] deterministic streams\n";
		auto draw = [](uint64_t seed, uint64_t pixel, uint64_t sample)
		{
			rng::begin_pixel_sample(seed, pixel, sample);
			std::vector<double> v(16);
			for (double& x : v) x = random_double();
			return v;
		};
		check(draw(7, 100, 3) == draw(7, 100, 3), "same (seed, pixel, sample) -> identical stream");
		check(draw(7, 100, 3) != draw(7, 100, 4), "different sample index -> different stream");
		check(draw(7, 100, 3) != draw(7, 101, 3), "different pixel -> different stream");
		check(draw(7, 100, 3) != draw(8, 100, 3), "different seed -> different stream");

		// Uniformity sanity check of the generator.
		rng::seed_thread(1);
		double sum = 0.0;
		const int n = 1000000;
		for (int i = 0; i < n; ++i) sum += random_double();
		const double mean = sum / n;
		check(std::abs(mean - 0.5) < 4.0 * std::sqrt(1.0 / 12.0 / n), "random_double mean = 0.5", "mean=" + fmt(mean, 6));

		bool all_perm = true;
		for (uint32_t len : { 1u, 2u, 7u, 16u, 31u, 961u, 1000u })
		{
			for (uint32_t seed : { 0u, 12345u, 0xdeadbeefu })
			{
				std::vector<int> seen(len, 0);
				for (uint32_t i = 0; i < len; ++i)
				{
					const uint32_t p = rng::permute(i, len, seed);
					if (p >= len) { all_perm = false; break; }
					seen[p]++;
				}
				for (int c : seen) if (c != 1) all_perm = false;
			}
		}
		check(all_perm, "stratum permutation is a bijection");
	}

	void test_pdf_normalization()
	{
		std::cout << "\n[pdf] normalization: integral of pdf over the sphere\n";
		rng::seed_thread(11);
		const Vector3 n = normalize(Vector3(0.3, 0.2, 0.9));

		{
			const Cosine_PDF p(n);
			const double integral = integrate_sphere_centered(n, 800, 256, [&](const Vector3& w) { return p.value(w); });
			check(std::abs(integral - 1.0) < 1e-3, "Cosine_PDF integral = 1", "integral=" + fmt(integral));
			const Chi2Result c = chi2_test(n, [&](Vector3& w) { w = p.generate(); return true; }, [&](const Vector3& w) { return p.value(w); }, 1000000);
			check(c.p_value > 0.01, "Cosine_PDF sample/pdf chi2", "chi2=" + fmt(c.chi2, 1) + " dof=" + std::to_string(c.dof) + " p=" + fmt(c.p_value, 3));
		}
		{
			const Sphere_PDF p{};
			const double integral = integrate_sphere_centered(n, 400, 128, [&](const Vector3& w) { return p.value(w); });
			check(std::abs(integral - 1.0) < 1e-3, "Sphere_PDF integral = 1 (was 4 before fix F2)", "integral=" + fmt(integral));
			const Chi2Result c = chi2_test(n, [&](Vector3& w) { w = p.generate(); return true; }, [&](const Vector3& w) { return p.value(w); }, 1000000);
			check(c.p_value > 0.01, "Sphere_PDF sample/pdf chi2", "chi2=" + fmt(c.chi2, 1) + " p=" + fmt(c.p_value, 3));
		}

		// GGX VNDF: invalid samples (reflection below the horizon) are returned and get pdf 0,
		// so integral(pdf) over valid directions must equal 1 - P(invalid).
		const double roughness_list[] = { 0.05, 0.1, 0.25, 0.5, 1.0 };
		const double cos_list[] = { 1.0, 0.70710678, 0.1 };
		for (double r : roughness_list)
		{
			for (double cv : cos_list)
			{
				const Vector3 zn(0, 0, 1);
				const Vector3 v = view_dir_for_cos(cv);
				const GGX_PDF p(zn, v, r);
				const Vector3 mirror = reflect(-v, zn);
				const double integral = integrate_sphere_centered(mirror, 2400, 512, [&](const Vector3& w) { return p.value(w); });

				const int samples = 400000;
				int invalid = 0;
				for (int s = 0; s < samples; ++s)
				{
					const Vector3 w = p.generate();
					if (p.value(w) <= 0.0)
						invalid++;
				}
				const double p_inv = static_cast<double>(invalid) / samples;
				const double sigma = std::sqrt(std::max(p_inv * (1.0 - p_inv), 1e-12) / samples);
				const double expected = 1.0 - p_inv;
				check(std::abs(integral - expected) < 4.0 * sigma + 3e-3,
					"GGX_PDF integral = 1 - P(invalid)  r=" + fmt(r, 2) + " cos_v=" + fmt(cv, 2),
					"integral=" + fmt(integral) + " 1-P(inv)=" + fmt(expected));
			}
		}

		for (double r : { 0.3, 0.6, 1.0 })
		{
			for (double cv : { 0.9, 0.4 })
			{
				const Vector3 zn(0, 0, 1);
				const GGX_PDF p(zn, view_dir_for_cos(cv), r);
				const Chi2Result c = chi2_test(zn,
					[&](Vector3& w) { w = p.generate(); return p.value(w) > 0.0; },
					[&](const Vector3& w) { return p.value(w); }, 1000000);
				check(c.p_value > 0.01, "GGX_PDF VNDF sample/pdf chi2  r=" + fmt(r, 2) + " cos_v=" + fmt(cv, 2),
					"chi2=" + fmt(c.chi2, 1) + " dof=" + std::to_string(c.dof) + " p=" + fmt(c.p_value, 3)
					+ " invalid obs/exp=" + fmt(c.invalid_observed, 0) + "/" + fmt(c.invalid_expected, 0));
			}
		}
	}

	void test_environment_pdf()
	{
		std::cout << "\n[pdf] environment importance sampling\n";
		rng::seed_thread(12);
		{
			const Constant_Environment env(Color(1, 1, 1));
			const double integral = integrate_sphere_centered(Vector3(0, 1, 0), 400, 128, [&](const Vector3& w) { return env.pdf_value(w); });
			check(std::abs(integral - 1.0) < 1e-3, "Constant_Environment pdf integral = 1", "integral=" + fmt(integral));
		}

		const LatLong_Environment env("images/HDR/suburban_garden_2k.hdr", 1.0, 0.0, false);
		// Deterministic integration in (u, v): d(omega) = 2 pi^2 sin(theta) du dv, 2x2 points per texel.
		const int W = 2048 * 2;
		const int H = 1024 * 2;
		double integral = 0.0;
		for (int y = 0; y < H; ++y)
		{
			const double v = (y + 0.5) / H;
			const double theta = v * pi;
			const double sin_theta = std::sin(theta);
			double row = 0.0;
			for (int x = 0; x < W; ++x)
			{
				const double u = (x + 0.5) / W;
				const double phi = u * 2.0 * pi - pi;
				const Vector3 d(std::cos(phi) * sin_theta, std::cos(theta), std::sin(phi) * sin_theta);
				row += env.pdf_value(d);
			}
			integral += row * 2.0 * pi * pi * sin_theta / (static_cast<double>(W) * H);
		}
		check(std::abs(integral - 1.0) < 1e-3, "LatLong_Environment pdf integral = 1 (suburban_garden_2k.hdr)", "integral=" + fmt(integral, 6));

		// Sampling vs pdf: histogram over a coarse lat-long grid. The expected mass of a bin is
		// the sum over its texels of pdf(texel centre) * texel solid angle (exact for a
		// piecewise-constant texel distribution).
		const int BW = 64, BH = 32;
		const int TW = 2048, TH = 1024;
		std::vector<double> expected(BW * BH, 0.0);
		for (int ty = 0; ty < TH; ++ty)
		{
			const double v = (ty + 0.5) / TH;
			const double theta = v * pi;
			const double texel_omega = 2.0 * pi * pi * std::sin(theta) / (static_cast<double>(TW) * TH);
			for (int tx = 0; tx < TW; ++tx)
			{
				const double u = (tx + 0.5) / TW;
				const double phi = u * 2.0 * pi - pi;
				const Vector3 d(std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta));
				expected[(ty * BH / TH) * BW + (tx * BW / TW)] += env.pdf_value(d) * texel_omega;
			}
		}
		const int N = 2000000;
		std::vector<double> observed(BW * BH, 0.0);
		for (int s = 0; s < N; ++s)
		{
			const Vector3 d = normalize(env.random());
			double phi = std::atan2(d.z(), d.x());
			double u = (phi + pi) / (2.0 * pi);
			u -= std::floor(u);
			const double v = std::acos(std::clamp(d.y(), -1.0, 1.0)) / pi;
			const int bx = std::clamp(static_cast<int>(u * BW), 0, BW - 1);
			const int by = std::clamp(static_cast<int>(v * BH), 0, BH - 1);
			observed[by * BW + bx] += 1.0;
		}
		double chi2 = 0.0;
		int cells = 0;
		double pooled_e = 0.0, pooled_o = 0.0;
		for (int b = 0; b < BW * BH; ++b)
		{
			const double e = expected[b] * N;
			if (e < 5.0) { pooled_e += e; pooled_o += observed[b]; continue; }
			chi2 += (observed[b] - e) * (observed[b] - e) / e;
			cells++;
		}
		if (pooled_e >= 5.0) { chi2 += (pooled_o - pooled_e) * (pooled_o - pooled_e) / pooled_e; cells++; }
		const double p = chi2_p_value(chi2, cells - 1);
		check(p > 0.01, "LatLong_Environment sample/pdf chi2", "chi2=" + fmt(chi2, 1) + " dof=" + std::to_string(cells - 1) + " p=" + fmt(p, 3));
	}

	void test_light_pdf()
	{
		std::cout << "\n[pdf] area light sampling (solid angle)\n";
		rng::seed_thread(13);
		const Point3 origin(0.3, -0.2, 0.1);
		auto emissive = std::make_shared<Diffuse_Light>(Color(1, 1, 1));

		const Quad quad(Point3(-1, 2, -1), Vector3(2, 0, 0), Vector3(0, 0, 2), emissive);
		const Sphere sphere(Point3(0.5, 0.4, -3.0), 0.7, emissive);

		auto run = [&](const Hittable& light, const std::string& name, double analytic_solid_angle)
		{
			// E_uniform[4 pi pdf] = integral(pdf) = 1
			const int N = 2000000;
			double sum = 0.0, sum2 = 0.0;
			for (int s = 0; s < N; ++s)
			{
				const double x = 4.0 * pi * light.pdf_value(origin, random_unit_vector());
				sum += x; sum2 += x * x;
			}
			const double mean = sum / N;
			const double sigma = std::sqrt(std::max(0.0, sum2 / N - mean * mean) / N);
			check(std::abs(mean - 1.0) < 4.0 * sigma + 1e-3, name + " pdf integral = 1", "integral=" + fmt(mean) + " +- " + fmt(sigma));

			// E_light[1 / pdf] = subtended solid angle
			double inv = 0.0;
			const int M = 200000;
			for (int s = 0; s < M; ++s)
			{
				const Vector3 d = light.random(origin);
				const double p = light.pdf_value(origin, d);
				if (p > 0.0) inv += 1.0 / p;
			}
			inv /= M;
			if (analytic_solid_angle > 0.0)
				check(std::abs(inv - analytic_solid_angle) / analytic_solid_angle < 5e-3, name + " E[1/pdf] = solid angle",
					"E[1/pdf]=" + fmt(inv) + " analytic=" + fmt(analytic_solid_angle));
		};

		run(quad, "Quad light", 0.0);
		const double d2 = (Point3(0.5, 0.4, -3.0) - origin).length_squared();
		run(sphere, "Sphere light", 2.0 * pi * (1.0 - std::sqrt(1.0 - 0.49 / d2)));
	}

	void test_light_sampler()
	{
		std::cout << "\n[light] light selection mixture (LightSampler)\n";
		rng::seed_thread(14);
		auto emissive = std::make_shared<Diffuse_Light>(Color(16, 15, 14));
		// README_Showcase-like configuration: one downward-facing area light + HDR environment.
		Hittable_List lights;
		lights.add(std::make_shared<Quad>(Point3(-4.0, 6.0, 2.8), Vector3(8.0, 0.0, 0.0), Vector3(0.0, 0.0, 3.8), emissive));
		lights.add(std::make_shared<Sphere>(Point3(3.0, 3.0, -1.0), 0.5, emissive));
		const LatLong_Environment env("images/HDR/suburban_garden_2k.hdr", 1.35, 0.0, false);

		LightSampler sampler;
		sampler.build(&lights, &env);
		const Point3 p(0.0, 0.18, 0.8);
		const Vector3 n(0, 1, 0);
		const LightSampler::Probabilities probs = sampler.probabilities(p, &n);
		const double sum = probs.env + probs.total_geo;
		check(std::abs(sum - 1.0) < 1e-12, "selection probabilities sum to 1",
			"env=" + fmt(probs.env, 4) + " geo=" + fmt(probs.total_geo, 4));
		check(probs.total_geo > 0.3 && probs.total_geo < 0.9,
			"area light gets a substantial share (was clamped to 0.05 before)", "geo=" + fmt(probs.total_geo, 4));
		std::cout << "  environment integral of luminance = " << fmt(env.integrated_luminance(), 4) << "\n";

		// The mixture density must integrate to 1 and match what sample() produces.
		const int N = 2000000;
		double s = 0.0, s2 = 0.0;
		for (int i = 0; i < N; ++i)
		{
			const double x = 4.0 * pi * sampler.pdf(probs, p, random_unit_vector());
			s += x; s2 += x * x;
		}
		const double mean = s / N;
		const double sigma = std::sqrt(std::max(0.0, s2 / N - mean * mean) / N);
		check(std::abs(mean - 1.0) < 4.0 * sigma + 2e-3, "light mixture pdf integrates to 1", "integral=" + fmt(mean) + " +- " + fmt(sigma));

		// Sample/pdf consistency of the mixture: if directions really follow p_mix, then for every
		// component density p_k, E_{w ~ p_mix}[p_k(w) / p_mix(w)] = integral p_k = 1. A wrong
		// selection probability or a pdf missing a factor breaks at least one of these.
		// (A binned chi2 is not used here: the cone / quad / texel discontinuities make the
		// expected bin counts too inaccurate at 2M samples.)
		{
			const int M = 1000000;
			double r_env = 0.0, r_quad = 0.0, r_sphere = 0.0;
			double q_env = 0.0, q_quad = 0.0, q_sphere = 0.0;
			for (int i = 0; i < M; ++i)
			{
				Vector3 w;
				if (!sampler.sample(probs, p, w))
					continue;
				const double pm = sampler.pdf(probs, p, w);
				if (pm <= 0.0)
					continue;
				const double a = env.pdf_value(w) / pm;
				const double b = lights.objects[0]->pdf_value(p, w) / pm;
				const double c = lights.objects[1]->pdf_value(p, w) / pm;
				r_env += a; q_env += a * a;
				r_quad += b; q_quad += b * b;
				r_sphere += c; q_sphere += c * c;
			}
			auto report = [&](double sum, double sum2, const std::string& name)
			{
				const double mean = sum / M;
				const double sigma = std::sqrt(std::max(0.0, sum2 / M - mean * mean) / M);
				check(std::abs(mean - 1.0) < 4.0 * sigma + 2e-3, "E_mix[p_" + name + " / p_mix] = 1",
					"value=" + fmt(mean) + " +- " + fmt(sigma));
			};
			report(r_env, q_env, "env");
			report(r_quad, q_quad, "quad");
			report(r_sphere, q_sphere, "sphere");
		}

		// Point behind the one-sided area light: its probability must be exactly 0.
		const Point3 above(0.0, 8.0, 4.0);
		const LightSampler::Probabilities behind = sampler.probabilities(above, &n);
		check(behind.geo[0] == 0.0, "one-sided light seen from behind gets probability 0");

		// No lights, no environment: nothing to sample.
		LightSampler none;
		Hittable_List empty;
		none.build(&empty, nullptr);
		check(!none.probabilities(p, &n).any, "empty light list -> NEE disabled (no fake (1,0,0) direction, Audit M2)");

		// Environment only: probability 1.
		LightSampler env_only;
		env_only.build(&empty, &env);
		check(std::abs(env_only.probabilities(p, &n).env - 1.0) < 1e-12, "environment-only scene -> P(env) = 1");
	}

	void test_sphere_tangent_frame()
	{
		std::cout << "\n[geometry] sphere tangent frame for normal mapping\n";
		rng::seed_thread(15);
		double worst_ortho = 0.0, worst_hand = 0.0, worst_du = 0.0;
		for (int i = 0; i < 20000; ++i)
		{
			const Vector3 n = random_unit_vector();
			HitRecord rec;
			Sphere::set_tangent_frame(n, rec);
			worst_ortho = std::max({ worst_ortho, std::abs(dot(rec.tangent, n)), std::abs(dot(rec.bitangent, n)),
				std::abs(dot(rec.tangent, rec.bitangent)), std::abs(rec.tangent.length() - 1.0), std::abs(rec.bitangent.length() - 1.0) });
			worst_hand = std::max(worst_hand, (cross(rec.tangent, rec.bitangent) - n).length());

			// T and B must point along +u and +v of get_sphere_uv (finite differences).
			if (std::abs(n.y()) < 0.99)
			{
				double u0, v0, u1, v1, u2, v2;
				Sphere::get_sphere_uv(n, u0, v0);
				Sphere::get_sphere_uv(normalize(n + 1e-5 * rec.tangent), u1, v1);
				Sphere::get_sphere_uv(normalize(n + 1e-5 * rec.bitangent), u2, v2);
				double du = u1 - u0;
				if (du > 0.5) du -= 1.0;
				if (du < -0.5) du += 1.0;
				if (!(du > 0.0 && std::abs(v1 - v0) < 1e-3 * std::abs(du) + 1e-9 && v2 - v0 > 0.0))
					worst_du = std::max(worst_du, 1.0);
			}
		}
		check(worst_ortho < 1e-9, "tangent / bitangent / normal orthonormal", "max error=" + fmt(worst_ortho, 12));
		check(worst_hand < 1e-9, "cross(T, B) = N (right-handed, OpenGL convention)", "max error=" + fmt(worst_hand, 12));
		check(worst_du == 0.0, "T follows +u and B follows +v of the sphere UV parameterization");

		// End to end: a normal-mapped PBR material on a Sphere must actually tilt the shading normal.
		const Sphere sphere(Point3(0, 0, 0), 1.0, nullptr);
		HitRecord rec;
		check(sphere.Hit(Ray(Point3(0.3, 0.2, 5.0), Vector3(0, 0, -1)), Interval(0.001, infinity), rec) && rec.has_tangent_space,
			"Sphere::Hit provides a tangent space (Audit H6)");
	}

	void test_bsdf()
	{
		std::cout << "\n[bsdf] Lambert / phase function\n";
		rng::seed_thread(21);
		const Vector3 n(0, 0, 1);
		{
			const BsdfUnderTest bsdf(make_query(std::make_shared<Lambertian>(Color(1, 1, 1)), n, view_dir_for_cos(0.6)));
			const AlbedoEstimate e = estimate_albedo_sampling(bsdf, 200000);
			const double q = albedo_quadrature(bsdf, n);
			check(std::abs(e.mean - 1.0) < 1e-9 && std::abs(q - 1.0) < 2e-3, "white Lambert furnace = 1",
				"sampled=" + fmt(e.mean, 6) + " quadrature=" + fmt(q, 6));
		}
		{
			const BsdfUnderTest bsdf(make_query(std::make_shared<isotropic>(Color(0.7, 0.7, 0.7)), Vector3(1, 0, 0), view_dir_for_cos(0.3)));
			const AlbedoEstimate e = estimate_albedo_sampling(bsdf, 100000);
			const double q = integrate_sphere_centered(Vector3(0, 0, 1), 400, 128, [&](const Vector3& wi)
			{
				const Color c = bsdf.eval(wi) * bsdf.cosine(wi);
				return (c.x() + c.y() + c.z()) / 3.0;
			});
			check(!bsdf.applies_cosine(), "isotropic phase has no cosine term (fix F3)");
			check(std::abs(e.mean - 0.7) < 1e-9 && std::abs(q - 0.7) < 1e-3, "isotropic phase albedo = 0.7 (fix F2/F3)",
				"sampled=" + fmt(e.mean, 6) + " quadrature=" + fmt(q, 6));
		}

		std::cout << "\n[bsdf] PBR sample/eval/pdf consistency + white furnace\n";
		std::cout << "  metallic  rough  cos_v   sampled(+-sigma)      quadrature   invalid  single-scatter ref (metal)\n";
		const double roughness_list[] = { 0.05, 0.1, 0.25, 0.5, 1.0 };
		const double cos_list[] = { 1.0, 0.70710678, 0.1 };
		for (double metallic : { 1.0, 0.0 })
		{
			for (double r : roughness_list)
			{
				for (double cv : cos_list)
				{
					const Vector3 v = view_dir_for_cos(cv);
					const BsdfUnderTest bsdf(make_query(solid_pbr(Color(1, 1, 1), r, metallic), n, v));
					const AlbedoEstimate e = estimate_albedo_sampling(bsdf, 200000);
					const double q = albedo_quadrature(bsdf, reflect(-v, n));
					const double ref = metallic > 0.5 ? reference_metal_albedo(r, cv) : -1.0;

					std::cout << "  " << fmt(metallic, 0) << "         " << fmt(r, 2) << "   " << fmt(cv, 2)
						<< "   " << fmt(e.mean, 4) << " +- " << fmt(e.sigma, 4)
						<< "   " << fmt(q, 4) << "      " << fmt(e.invalid_fraction, 3)
						<< (ref >= 0.0 ? "    " + fmt(ref, 4) : std::string("")) << "\n";

					const std::string tag = " m=" + fmt(metallic, 0) + " r=" + fmt(r, 2) + " cos_v=" + fmt(cv, 2);
					// Sample and Eval/PDF describe the same distribution: two independent estimators
					// of the same integral must agree.
					check(std::abs(e.mean - q) < 4.0 * e.sigma + 3e-3 * std::max(q, 0.1), "sample/eval/pdf consistent" + tag,
						"sampled=" + fmt(e.mean, 4) + " quadrature=" + fmt(q, 4));
					// Energy conservation: a white BSDF must not reflect more than it receives.
					check(q <= 1.0 + 3e-3, "white furnace albedo <= 1" + tag, "albedo=" + fmt(q, 4));
					if (metallic > 0.5)
					{
						// Masking-shadowing must match the Smith model of the VNDF sampler.
						check(std::abs(q - ref) < 5e-3 + 0.01 * ref, "metal albedo matches exact height-correlated Smith" + tag,
							"albedo=" + fmt(q, 4) + " reference=" + fmt(ref, 4));
					}
					else
					{
						// White dielectric: energy the specular lobe does not reflect goes to the
						// diffuse layer, so nothing is lost or gained.
						check(std::abs(q - 1.0) < 0.01, "white dielectric furnace albedo = 1" + tag, "albedo=" + fmt(q, 4));
					}
				}
			}
		}

		// Full PBR mixture (energy-based lobe selection + cosine + VNDF): the directions produced
		// by BSDF::sample() follow BSDF::pdf(), including the invalid-sample mass.
		for (double metallic : { 0.0, 0.5, 1.0 })
		{
			for (double r : { 0.3, 0.8 })
			{
				for (double cv : { 0.9, 0.3 })
				{
					const BsdfUnderTest bsdf(make_query(solid_pbr(Color(0.8, 0.6, 0.4), r, metallic), n, view_dir_for_cos(cv)));
					const Chi2Result c = chi2_test(n,
						[&](Vector3& w) { const BsdfSampleResult s = bsdf.sample(); w = s.wi; return s.valid; },
						[&](const Vector3& w) { return bsdf.pdf(w); }, 1000000);
					check(c.p_value > 0.01, "BSDF::sample follows BSDF::pdf (chi2) m=" + fmt(metallic, 1) + " r=" + fmt(r, 1) + " cos_v=" + fmt(cv, 1),
						"chi2=" + fmt(c.chi2, 1) + " dof=" + std::to_string(c.dof) + " p=" + fmt(c.p_value, 3)
						+ " p_spec=" + fmt(bsdf.get().specular_probability(), 3));
				}
			}
		}

		// Reciprocity (no normal map): f(wo, wi) = f(wi, wo).
		rng::seed_thread(22);
		double worst = 0.0;
		for (int t = 0; t < 2000; ++t)
		{
			const Vector3 wo = normalize(Vector3(random_double(-1, 1), random_double(-1, 1), random_double(0.05, 1)));
			const Vector3 wi = normalize(Vector3(random_double(-1, 1), random_double(-1, 1), random_double(0.05, 1)));
			const double r = random_double(0.05, 1.0);
			const double m = random_double(0.0, 1.0);
			auto mat = solid_pbr(Color(0.8, 0.6, 0.4), r, m);
			const Color a = BsdfUnderTest(make_query(mat, n, wo)).eval(wi);
			const Color b = BsdfUnderTest(make_query(mat, n, wi)).eval(wo);
			for (int c = 0; c < 3; ++c)
				worst = std::max(worst, std::abs(a[c] - b[c]) / std::max(1e-6, std::abs(a[c]) + std::abs(b[c])));
		}
		check(worst < 1e-6, "PBR BRDF reciprocity f(wo,wi) = f(wi,wo)", "max relative diff=" + fmt(worst, 8));

		// Random configurations: sample/eval/pdf agree at the sampled direction.
		double worst_pdf = 0.0;
		for (int t = 0; t < 200; ++t)
		{
			const Vector3 wo = normalize(Vector3(random_double(-1, 1), random_double(-1, 1), random_double(0.02, 1)));
			const BsdfUnderTest bsdf(make_query(solid_pbr(Color(0.9, 0.9, 0.9), random_double(0.05, 1.0), random_double()), n, wo));
			for (int s = 0; s < 200; ++s)
			{
				const BsdfSampleResult r = bsdf.sample();
				if (!r.valid) continue;
				worst_pdf = std::max(worst_pdf, std::abs(r.pdf - bsdf.pdf(r.wi)) / r.pdf);
			}
		}
		check(worst_pdf < 1e-9, "sampled pdf equals pdf(wo, wi) evaluated separately", "max relative diff=" + fmt(worst_pdf, 10));
	}

	void test_texture_decode()
	{
		std::cout << "\n[texture] color space decoding (fix F1)\n";
		std::filesystem::create_directories("output");
		const std::string path = "output/__test_gray128.ppm";
		{
			std::ofstream out(path, std::ios::binary);
			out << "P6\n1 1\n255\n";
			const unsigned char px[3] = { 128, 128, 128 };
			out.write(reinterpret_cast<const char*>(px), 3);
		}
		const Image_Texture linear_tex(path, color_space::Linear);
		const Image_Texture srgb_tex(path, color_space::SRGB);
		const double lin = linear_tex.value(0.5, 0.5, Point3()).x();
		const double srgb = srgb_tex.value(0.5, 0.5, Point3()).x();
		std::filesystem::remove(path);
		check(std::abs(lin - 128.0 / 255.0) < 2.0 / 255.0, "Linear texture keeps the encoded value (128 -> 0.502)", "value=" + fmt(lin, 4));
		check(std::abs(srgb - 0.2158) < 0.004, "sRGB texture decodes once (128 -> 0.216)", "value=" + fmt(srgb, 4));
	}

	void test_determinism()
	{
		std::cout << "\n[render] determinism across thread counts\n";
		auto render = [](uint64_t seed, int threads)
		{
			const SceneEntry* entry = find_scene("cornell_small");
			rng::seed_thread(seed);
			SceneDesc scene = entry->build();
			scene.cam.image_width = 32;
			scene.cam.sample_per_pixel = 8;
			scene.cam.seed = seed;
			scene.cam.thread_count = threads;
			scene.cam.write_outputs = false;
			scene.cam.verbose = false;
			scene.cam.Render(scene.world, scene.lights);
			return scene.cam.LastFramebuffer();
		};
		const auto a = render(1234, 1);
		const auto b = render(1234, 3);
		const auto c = render(1234, 0);
		const auto d = render(1235, 0);
		auto identical = [](const std::vector<Color>& x, const std::vector<Color>& y)
		{
			if (x.size() != y.size()) return false;
			for (size_t i = 0; i < x.size(); ++i)
				for (int k = 0; k < 3; ++k)
					if (x[i][k] != y[i][k]) return false;
			return true;
		};
		check(identical(a, b) && identical(a, c), "same seed -> bit-identical image for 1 / 3 / all threads");
		check(!identical(a, d), "different seed -> different image");
	}

	void test_hot_path_allocations()
	{
		std::cout << "\n[perf] heap allocations in the path-tracing hot path (Audit P-1)\n";
		for (const char* name : { "cornell_small", "normal_map_small", "environment_small", "volume_small" })
		{
			const SceneEntry* entry = find_scene(name);
			rng::seed_thread(3);
			SceneDesc scene = entry->build();
			LightSampler sampler;
			sampler.build(&scene.lights, scene.cam.GetEnvironment().get());
			PathIntegrator::Settings settings;
			settings.max_depth = 16;
			const PathIntegrator integrator(scene.world, sampler, scene.cam.GetEnvironment().get(), Color(0, 0, 0), settings);
			const Point3 origin = scene.cam.lookfrom;
			const Vector3 forward = normalize(scene.cam.lookat - scene.cam.lookfrom);
			integrator.Li(Ray(origin, forward)); // warm-up (static tables)

			const uint64_t before = heap_allocation_count();
			const int rays = 20000;
			double sink = 0.0;
			for (int k = 0; k < rays; ++k)
			{
				rng::begin_pixel_sample(1, k, 0);
				const Vector3 jitter(random_double(-0.3, 0.3), random_double(-0.3, 0.3), random_double(-0.3, 0.3));
				sink += integrator.Li(Ray(origin, normalize(forward + jitter))).x();
			}
			const uint64_t allocations = heap_allocation_count() - before;
			check(allocations == 0, std::string("zero heap allocations per camera path: ") + name,
				std::to_string(allocations) + " allocations for " + std::to_string(rays) + " paths (sink " + fmt(sink / rays, 3) + ")");
		}
	}

	void test_russian_roulette()
	{
		std::cout << "\n[render] Russian roulette is unbiased\n";
		// Deep diffuse interreflection (Cornell box) and a scattering medium, rendered with and
		// without RR. Means must agree within the Monte Carlo error; RR must shorten paths.
		for (const char* name : { "cornell_small", "volume_small" })
		{
			auto render = [&](bool rr, uint64_t seed, RenderCounters& counters)
			{
				const SceneEntry* entry = find_scene(name);
				rng::seed_thread(99);
				SceneDesc scene = entry->build();
				scene.cam.image_width = 32;
				scene.cam.sample_per_pixel = 256;
				scene.cam.max_depth = 64;
				scene.cam.russian_roulette = rr;
				scene.cam.seed = seed;
				scene.cam.write_outputs = false;
				scene.cam.verbose = false;
				scene.cam.Render(scene.world, scene.lights);
				counters = scene.cam.LastCounters();
				return scene.cam.LastFramebuffer();
			};
			RenderCounters c_off, c_on;
			const auto off = render(false, 7, c_off);
			const auto on = render(true, 8, c_on);
			// Standard error of the image mean from the per-pixel differences.
			double sum_d = 0.0, sum_d2 = 0.0;
			for (size_t i = 0; i < off.size(); ++i)
			{
				const double d = (on[i].x() + on[i].y() + on[i].z() - off[i].x() - off[i].y() - off[i].z()) / 3.0;
				sum_d += d;
				sum_d2 += d * d;
			}
			const double n_px = static_cast<double>(off.size());
			const double mean_d = sum_d / n_px;
			const double se = std::sqrt(std::max(0.0, sum_d2 / n_px - mean_d * mean_d) / n_px);
			const double mean_off = image_mean(off);
			check(std::abs(mean_d) < 4.0 * se + 1e-4, std::string("RR on/off image means agree: ") + name,
				"mean off=" + fmt(mean_off, 5) + " on=" + fmt(image_mean(on), 5) + " diff=" + fmt(mean_d, 5) + " +- " + fmt(se, 5));
			const double len_off = static_cast<double>(c_off.path_vertices) / c_off.camera_rays;
			const double len_on = static_cast<double>(c_on.path_vertices) / c_on.camera_rays;
			check(len_on < len_off, std::string("RR shortens paths: ") + name,
				"vertices/path off=" + fmt(len_off, 2) + " on=" + fmt(len_on, 2));
		}
	}
}

int run_unit_tests(const std::string& filter)
{
	struct Entry { const char* name; void (*fn)(); };
	const Entry tests[] = {
		{ "rng", test_rng },
		{ "pdf", test_pdf_normalization },
		{ "environment", test_environment_pdf },
		{ "light", test_light_pdf },
		{ "lightsampler", test_light_sampler },
		{ "tangent", test_sphere_tangent_frame },
		{ "bsdf", test_bsdf },
		{ "texture", test_texture_decode },
		{ "determinism", test_determinism },
		{ "roulette", test_russian_roulette },
		{ "alloc", test_hot_path_allocations },
	};

	for (const Entry& t : tests)
	{
		if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos)
			continue;
		t.fn();
	}

	std::cout << "\n" << (g_state.failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
		<< ": " << (g_state.checks - g_state.failures) << " / " << g_state.checks << " checks passed\n";
	for (const std::string& name : g_state.failed)
		std::cout << "  failed: " << name << "\n";
	return g_state.failures;
}

// ============================================================================================
// Rendering regression
// ============================================================================================
namespace
{
	struct RegressionCase
	{
		std::string scene;
		int spp;
		double analytic = -1.0; // >= 0: every pixel must equal this value
	};

	const std::vector<RegressionCase>& regression_cases()
	{
		static const std::vector<RegressionCase> cases = {
			{ "furnace_lambert", 64, 1.0 },
			{ "furnace_volume", 64, 1.0 },
			{ "furnace_pbr", 64 },
			{ "cornell_small", 64 },
			{ "volume_small", 64 },
			{ "normal_map_small", 64 },
			{ "environment_small", 64 },
		};
		return cases;
	}

	constexpr uint64_t kRegressionSeed = 20260930;

	LinearImage render_case(const RegressionCase& c, int spp, uint64_t seed, double& seconds)
	{
		const SceneEntry* entry = find_scene(c.scene);
		rng::seed_thread(kRegressionSeed);
		SceneDesc scene = entry->build();
		scene.cam.sample_per_pixel = spp;
		scene.cam.seed = seed;
		scene.cam.thread_count = 0;
		scene.cam.write_outputs = false;
		scene.cam.verbose = false;
		const auto start = std::chrono::steady_clock::now();
		if (scene.use_lights)
			scene.cam.Render(scene.world, scene.lights);
		else
			scene.cam.Render(scene.world);
		seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		LinearImage image;
		image.width = scene.cam.image_width;
		image.height = scene.cam.output_height();
		image.pixels = scene.cam.LastFramebuffer();
		return image;
	}

	struct Expectation
	{
		double rel_mse = 0.0;
		double mean_rel_diff = 0.0;
	};

	std::map<std::string, Expectation> load_expectations(const std::string& path)
	{
		std::map<std::string, Expectation> result;
		std::ifstream in(path);
		std::string name;
		Expectation e;
		while (in >> name >> e.rel_mse >> e.mean_rel_diff)
			result[name] = e;
		return result;
	}
}

int run_regression(const RegressionOptions& options)
{
	namespace fs = std::filesystem;
	const std::string expectations_path = options.reference_dir + "/regression.txt";
	std::map<std::string, Expectation> expectations = load_expectations(expectations_path);
	if (options.update_references)
		fs::create_directories(options.reference_dir);

	int failures = 0;
	std::cout << "Regression (seed " << kRegressionSeed << ")\n"
		<< "| Scene | SPP | Time (s) | RMSE | relMSE | expected relMSE | mean | ref mean | Result |\n"
		<< "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";

	for (const RegressionCase& c : regression_cases())
	{
		const std::string ref_path = options.reference_dir + "/" + c.scene + ".pfm";
		LinearImage reference;

		if (options.update_references && c.analytic < 0.0)
		{
			const int ref_spp = options.reference_spp > 0 ? options.reference_spp : 16 * c.spp;
			double ref_seconds = 0.0;
			reference = render_case(c, ref_spp, kRegressionSeed ^ 0x5EED5EEDull, ref_seconds);
			image_io::write_pfm(ref_path, reference);
			std::clog << "  reference " << c.scene << ": " << ref_spp << " spp in " << fmt(ref_seconds, 2) << " s\n";
		}

		double seconds = 0.0;
		const LinearImage test_image = render_case(c, c.spp, kRegressionSeed, seconds);
		const std::vector<Color>& test = test_image.pixels;

		bool ok = true;
		ImageMetrics m;
		double expected_rel = 0.0;
		if (c.analytic >= 0.0)
		{
			reference.pixels.assign(test.size(), Color(c.analytic, c.analytic, c.analytic));
			m = compare_images(test, reference.pixels);
			// Mean over all pixels must match the analytic value; the per-pixel error only
			// contains Monte Carlo noise.
			ok = std::abs(m.mean_test - c.analytic) < 0.005 * c.analytic && m.non_finite == 0;
		}
		else
		{
			if (!image_io::read_pfm(ref_path, reference) || reference.pixels.size() != test.size())
			{
				std::cout << "| " << c.scene << " | " << c.spp << " | - | - | - | - | - | - | MISSING REFERENCE (run --regress --update-references) |\n";
				failures++;
				continue;
			}
			m = compare_images(test, reference.pixels);
			const double mean_rel_diff = std::abs(m.mean_test - m.mean_reference) / std::max(1e-6, m.mean_reference);
			if (options.update_references)
				expectations[c.scene] = Expectation{ m.rel_mse, mean_rel_diff };
			const auto it = expectations.find(c.scene);
			if (it == expectations.end())
			{
				ok = false;
			}
			else
			{
				expected_rel = it->second.rel_mse;
				// Noise may differ after an unbiased change, but must not grow much; the image
				// mean detects energy bias that per-pixel noise would hide.
				ok = m.rel_mse <= 1.5 * expected_rel + 1e-6 &&
					mean_rel_diff <= std::max(0.01, 3.0 * it->second.mean_rel_diff) &&
					m.non_finite == 0;
			}
		}

		if (!ok)
			failures++;
		std::cout << "| " << c.scene << " | " << c.spp << " | " << fmt(seconds, 2)
			<< " | " << fmt(m.rmse, 5) << " | " << fmt(m.rel_mse, 6) << " | "
			<< (c.analytic >= 0.0 ? std::string("analytic ") + fmt(c.analytic, 1) : fmt(expected_rel, 6))
			<< " | " << fmt(m.mean_test, 5) << " | " << fmt(m.mean_reference, 5)
			<< " | " << (ok ? "PASS" : "FAIL") << " |\n" << std::flush;
	}

	if (options.update_references)
	{
		std::ofstream out(expectations_path);
		for (const auto& [name, e] : expectations)
			out << name << ' ' << std::setprecision(9) << e.rel_mse << ' ' << e.mean_rel_diff << '\n';
		std::cout << "Updated " << expectations_path << "\n";
	}

	std::cout << (failures == 0 ? "REGRESSION PASSED\n" : "REGRESSION FAILED\n");
	return failures;
}
