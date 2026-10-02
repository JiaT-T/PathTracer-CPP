#pragma once
#include <algorithm>
#include <cmath>

#include "My_Common.h"
#include "Hittable.h"
#include "Material.h"
#include "BSDF.h"
#include "Environment.h"
#include "LightSampler.h"
#include "Stats.h"

inline double power_heuristic(double pdf_a, double pdf_b)
{
	// Balance two estimators for MIS. Squared PDFs give the power heuristic.
	const double a2 = pdf_a * pdf_a;
	const double b2 = pdf_b * pdf_b;
	return a2 <= 0.0 ? 0.0 : a2 / (a2 + b2);
}

// Power heuristic for multi-sample MIS: strategy a takes count_a samples, strategy b count_b.
inline double power_heuristic(double pdf_a, int count_a, double pdf_b, int count_b)
{
	return power_heuristic(count_a * pdf_a, count_b * pdf_b);
}

inline double max_component(const Color& c)
{
	return std::max(c.x(), std::max(c.y(), c.z()));
}

inline double luminance(const Color& c)
{
	return 0.2126 * c.x() + 0.7152 * c.y() + 0.0722 * c.z();
}

// Per-sample data recorded along a camera path for AOVs, the denoiser guide and debugging.
struct PathSample
{
	Color L = Color(0, 0, 0);

	// First camera-ray hit
	bool hit = false;
	Color albedo = Color(0, 0, 0);
	Vector3 shading_normal = Vector3(0, 0, 0);   // normal used by the BSDF (normal-mapped)
	Vector3 geometry_normal = Vector3(0, 0, 0);
	double depth = 0.0;                          // distance along the camera ray
	double roughness = 0.0;
	double metallic = 0.0;

	// Radiance split by the number of scattering events before reaching the emitter
	Color emission = Color(0, 0, 0);   // 0 scattering events (emitters / environment seen directly)
	Color direct = Color(0, 0, 0);     // exactly 1
	Color indirect = Color(0, 0, 0);   // 2 or more

	// First vertex sampling data (0 if the first vertex is delta / absent)
	double bsdf_pdf = 0.0;             // pdf of the continuation direction
	double light_pdf = 0.0;            // light-mixture pdf of the first NEE sample
	double mis_weight = 0.0;           // MIS weight of the first NEE sample
	int path_length = 0;               // scattering events before termination

	// Denoiser guide: first non-delta vertex (seen through mirrors / glass), albedo includes
	// the delta-chain attenuation (Audit section 7, item 4).
	bool guide_valid = false;
	bool guide_emitter = false;        // guide vertex is an emitter (path ends there)
	double guide_glossiness = 0.0;     // p_spec * (1 - roughness)^2 of the guide vertex
	Color guide_albedo = Color(0, 0, 0);
	Vector3 guide_normal = Vector3(0, 0, 0);
	double guide_depth = 0.0;
};

// Iterative unidirectional path tracer with next-event estimation and MIS.
//
// Estimator (unchanged in structure from the previous recursive integrator, which was verified
// by the Audit): at every non-delta vertex, N_L light-mixture samples (4 at the first vertex,
// 1 afterwards) and one BSDF sample, combined with the multi-sample power heuristic
//   w_L = (N_L p_L)^2 / ((N_L p_L)^2 + p_B^2),  w_B = p_B^2 / ((N_L p_L)^2 + p_B^2).
// Emission reached by the camera ray or after a delta event is added with weight 1; emission
// reached by a BSDF sample from a non-delta vertex is added with w_B (the light strategy covers
// the rest). Difference to the previous code: the BSDF sample used for MIS is the path
// continuation itself, instead of an extra ray traced only for direct lighting (Audit N2 / P-4:
// one ray less per bounce, same expected value).
//
// Russian roulette (Audit N7): from bounce rr_start_bounce on, the path survives with
// probability q = min(0.95, max component of the throughput) and the throughput is divided
// by q, which keeps the estimator unbiased.
class PathIntegrator
{
public:
	struct Settings
	{
		int max_depth = 10;                  // maximum number of scattering events
		int first_bounce_light_samples = 4;  // NEE samples at the camera-visible vertex
		bool russian_roulette = true;
		int rr_start_bounce = 3;
	};

	PathIntegrator(const Hittable& world, const LightSampler& lights, const Environment* environment,
		const Color& background, const Settings& settings)
		: world(world), lights(lights), environment(environment), background(background), settings(settings) {}

	Color Li(const Ray& camera_ray, PathSample* record = nullptr) const
	{
		RenderCounters& counters = thread_counters();
		counters.camera_rays++;

		Color L(0, 0, 0);
		Color beta(1, 1, 1);
		Ray ray = camera_ray;

		// MIS state of the previous vertex (for emission found by the BSDF sample)
		bool specular_bounce = true;       // camera ray, or previous vertex was a delta event
		double prev_bsdf_pdf = 0.0;
		int prev_light_samples = 0;
		Point3 prev_point;
		LightSampler::Probabilities prev_probs;
		Color delta_chain(1, 1, 1);        // attenuation through delta events before the guide vertex
		double path_distance = 0.0;        // distance travelled along the path (guide depth)

		for (int bounce = 0; ; ++bounce)
		{
			HitRecord rec;
			const bool hit = world.Hit(ray, Interval(0.001, infinity), rec);

			// Emission at this vertex, or environment / background on a miss.
			Environment::LookupCache environment_cache;
			const Color Le = hit ? rec.mat->emitted(ray, rec, rec.u, rec.v, rec.p) : miss_radiance(ray, environment_cache);
			if (Le.x() > 0.0 || Le.y() > 0.0 || Le.z() > 0.0)
			{
				double w = 1.0;
				if (!specular_bounce && prev_light_samples > 0)
				{
					const double light_pdf = lights.pdf(prev_probs, prev_point, ray.direction(), &environment_cache);
					w = power_heuristic(prev_bsdf_pdf, 1, light_pdf, prev_light_samples);
				}
				add(L, record, bounce, beta * Le * w);
			}

			if (!hit)
				break;

			if (record && bounce == 0)
			{
				record->hit = true;
				record->depth = rec.t * ray.direction().length();
				record->geometry_normal = rec.geo_n;
				record->shading_normal = rec.n;
			}

			BSDF bsdf;
			const bool scatters = bounce < settings.max_depth && rec.mat->GetBSDF(ray, rec, bsdf);
			if (record)
			{
				const bool surface = scatters && !bsdf.is_delta();
				if (bounce == 0)
				{
					// Emitters / delta surfaces: material albedo; otherwise the BSDF's (no refetch).
					record->albedo = surface ? bsdf.base_albedo() : rec.mat->Albedo(rec.u, rec.v, rec.p);
					if (surface && bsdf.applies_cosine())
					{
						record->shading_normal = bsdf.shading_normal();
						record->roughness = bsdf.kind == BSDF::Kind::PBR ? bsdf.lobe_roughness() : 1.0;
						record->metallic = bsdf.kind == BSDF::Kind::PBR ? bsdf.lobe_metallic() : 0.0;
					}
				}
				if (!record->guide_valid && (surface || !scatters))
				{
					// First non-delta vertex (or where the path ends): denoiser guide.
					record->guide_valid = true;
					record->guide_emitter = !scatters && (Le.x() > 0.0 || Le.y() > 0.0 || Le.z() > 0.0);
					if (surface && bsdf.kind == BSDF::Kind::PBR)
					{
						const double smooth = 1.0 - bsdf.lobe_roughness();
						record->guide_glossiness = bsdf.specular_probability() * smooth * smooth;
					}
					record->guide_albedo = delta_chain * (surface ? bsdf.base_albedo() : rec.mat->Albedo(rec.u, rec.v, rec.p));
					// Media have no normal (rec.n is an arbitrary (1,0,0), which equals e.g. the
					// Cornell red wall's normal); use the view-facing direction so medium pixels
					// group with each other and not with surfaces.
					record->guide_normal = !surface ? rec.geo_n
						: bsdf.applies_cosine() ? bsdf.shading_normal()
						: -normalize(ray.direction());
					record->guide_depth = path_distance + rec.t * ray.direction().length();
				}
			}
			path_distance += rec.t * ray.direction().length();
			if (!scatters)
				break; // max depth, or absorbed (emitter, or scattering below the surface)
			counters.path_vertices++;
			if (record)
				record->path_length = bounce + 1;

			if (bsdf.is_delta())
			{
				BSDFSample s;
				bsdf.sample(s);
				beta = beta * s.f;
				delta_chain = delta_chain * s.f;
				ray = Ray(rec.p, s.wi, ray.time());
				specular_bounce = true;
				prev_light_samples = 0;
				counters.bounce_rays++;
			}
			else
			{
				// Next-event estimation
				const LightSampler::Probabilities probs =
					lights.probabilities(rec.p, bsdf.applies_cosine() ? &bsdf.shading_normal() : nullptr);
				const int light_samples = probs.any ? (bounce == 0 ? settings.first_bounce_light_samples : 1) : 0;
				for (int i = 0; i < light_samples; ++i)
				{
					Vector3 wi;
					if (!lights.sample(probs, rec.p, wi))
						continue;
					Environment::LookupCache shadow_environment_cache;
					const double light_pdf = lights.pdf(probs, rec.p, wi, &shadow_environment_cache);
					if (!(light_pdf > 0.0))
						continue;
					const double cos_theta = bsdf.cosine(wi);
					if (cos_theta <= 0.0)
						continue;
					const Color f = bsdf.eval(wi);
					if (f.x() <= 0.0 && f.y() <= 0.0 && f.z() <= 0.0)
						continue;
					const double bsdf_pdf = bsdf.pdf(wi);
					const double w = power_heuristic(light_pdf, light_samples, bsdf_pdf, 1);
					if (record && bounce == 0 && i == 0)
					{
						record->light_pdf = light_pdf;
						record->mis_weight = w;
					}
					const Color Ld = trace_emitted(Ray(rec.p, wi, ray.time()), shadow_environment_cache);
					add(L, record, bounce + 1, beta * f * Ld * (cos_theta * w / (light_pdf * light_samples)));
				}

				// BSDF sample = path continuation (also the BSDF strategy of the MIS above)
				BSDFSample s;
				if (!bsdf.sample(s))
					break;
				const double cos_theta = bsdf.cosine(s.wi);
				if (cos_theta <= 0.0)
					break;
				if (record && bounce == 0)
					record->bsdf_pdf = s.pdf;
				beta = beta * s.f * (cos_theta / s.pdf);

				specular_bounce = false;
				prev_bsdf_pdf = s.pdf;
				prev_light_samples = light_samples;
				prev_point = rec.p;
				prev_probs = probs;
				ray = Ray(rec.p, s.wi, ray.time());
				counters.bounce_rays++;
			}

			if (settings.russian_roulette && bounce >= settings.rr_start_bounce)
			{
				const double survival = std::min(0.95, max_component(beta));
				if (!(survival > 0.0) || random_double() >= survival)
					break;
				beta = beta / survival;
			}
		}

		if (record)
			record->L = L;
		return L;
	}

private:
	const Hittable& world;
	const LightSampler& lights;
	const Environment* environment;
	Color background;
	Settings settings;

	Color miss_radiance(const Ray& ray, Environment::LookupCache& cache) const
	{
		// If an HDR environment is set, it becomes both background and light source.
		return environment ? environment->radiance(ray.direction(), cache) : background;
	}

	// Radiance arriving along a shadow ray: emission of the closest hit, or the environment.
	Color trace_emitted(const Ray& shadow_ray, Environment::LookupCache& cache) const
	{
		thread_counters().shadow_rays++;
		HitRecord light_rec;
		if (!world.Hit(shadow_ray, Interval(0.001, infinity), light_rec))
			return miss_radiance(shadow_ray, cache);
		return light_rec.mat->emitted(shadow_ray, light_rec, light_rec.u, light_rec.v, light_rec.p);
	}

	// scattering_events: 0 = seen directly, 1 = direct lighting, >= 2 indirect.
	static void add(Color& L, PathSample* record, int scattering_events, const Color& c)
	{
		L += c;
		if (!record)
			return;
		if (scattering_events == 0) record->emission += c;
		else if (scattering_events == 1) record->direct += c;
		else record->indirect += c;
	}
};
