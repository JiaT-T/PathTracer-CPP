#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Hittable.h"
#include "Hittable_List.h"
#include "Environment.h"

// Light selection for next-event estimation.
//
// Strategies: every sampleable geometry light in the `lights` list, plus the environment.
// At a shading point p, strategy i is chosen with probability P_i(p) and then samples a
// direction with its own solid-angle pdf p_i. The resulting direction density is the mixture
//     p_light(w) = sum_i P_i(p) p_i(w)
// which is used for both the NEE estimator and the MIS weight of BSDF samples, so the
// estimator stays unbiased for any P_i > 0 wherever strategy i can contribute.
//
// Importance estimate (dimension: radiance x solid angle, i.e. the unoccluded irradiance
// order of magnitude, independent of image resolution):
//   geometry light:  E_i = L_i * Omega_i(p),  Omega = A |cos_l| / d^2 (planar, <= 2 pi) or the
//                    exact cone solid angle (sphere). One-sided emitters seen from behind: 0.
//   environment:     E_env = 1/2 * integral L dw   (a surface sees one hemisphere; the full
//                    integral is used at volume scattering points)
// The previous code compared the environment's *pixel sum* (proportional to the texture
// resolution) with the lights' L * A, which are different units; README_Showcase ended up with
// 0.05 / 0.95 purely because of the clamp (Audit M1).
//
// A defensive mixture P_i = (1 - d) E_i / sum E + d / n_active (d = 0.1) keeps every
// strategy that can contribute from being starved by a poor (visibility-blind) estimate.
class LightSampler
{
public:
	static constexpr int kMaxLocal = 32;          // per-point probabilities up to this many lights
	static constexpr double kDefensive = 0.1;

	struct Probabilities
	{
		int count = 0;                              // number of geometry lights
		std::array<double, kMaxLocal> geo{};        // P_i (only when local)
		double env = 0.0;                           // P_env
		double total_geo = 0.0;                     // sum of geometry probabilities
		bool any = false;                           // at least one strategy can contribute
	};

	LightSampler() = default;

	void build(const Hittable_List* lights, const Environment* environment)
	{
		entries.clear();
		env = environment;
		env_estimate = 0.0;
		if (lights)
		{
			for (const auto& object : lights->objects)
			{
				Entry e;
				e.object = object.get();
				if (!object->light_shape_info(e.info))
				{
					// Transform wrappers, meshes, lists: pdf_value()/random() are not implemented
					// (Audit M2). Sampling them would return a fake direction with pdf 0.
					std::clog << "Warning: object in the light list cannot be sampled as a light; ignored.\n";
					continue;
				}
				// Sampling-only proxies (e.g. Cornell_Box adds material-less copies of the
				// emitter) carry no emission information: treat them as unit radiance.
				e.known_emission = e.info.luminance > 0.0;
				if (!e.known_emission)
					e.info.luminance = 1.0;
				entries.push_back(e);
			}
		}
		if (env)
			env_estimate = std::max(0.0, env->integrated_luminance());

		// Position-independent fallback for very large light lists.
		global_geo.assign(entries.size(), 0.0);
		double sum = 0.0;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			global_geo[i] = entries[i].info.luminance * std::max(entries[i].info.area, 1e-12);
			sum += global_geo[i];
		}
		for (double& g : global_geo)
			g = sum > 0.0 ? g / sum : 1.0 / entries.size();
	}

	bool empty() const { return entries.empty() && env == nullptr; }
	int geometry_light_count() const { return static_cast<int>(entries.size()); }

	// Selection probabilities at p. normal = nullptr at volume scattering points.
	Probabilities probabilities(const Point3& p, const Vector3* normal) const
	{
		Probabilities out;
		out.count = static_cast<int>(entries.size());
		const bool local = out.count <= kMaxLocal;

		double estimates_sum = 0.0;
		int active = 0;
		std::array<double, kMaxLocal> estimate{};
		if (local)
		{
			for (int i = 0; i < out.count; ++i)
			{
				estimate[i] = geometry_estimate(entries[i], p);
				estimates_sum += estimate[i];
				if (estimate[i] > 0.0)
					active++;
			}
		}
		else
		{
			// Too many lights for per-point estimates: use the static power distribution.
			active = out.count;
			estimates_sum = 1.0;
		}

		double env_e = 0.0;
		if (env)
		{
			env_e = normal ? 0.5 * env_estimate : env_estimate;
			if (!(env_e > 0.0))
				env_e = 1e-12; // an environment can always contribute; keep it sampleable
			active++;
		}

		const double total = estimates_sum + (local ? env_e : 0.0);
		if (active == 0 || !(total > 0.0))
			return out; // no strategy can contribute: NEE is skipped (BSDF MIS weight = 1)

		out.any = true;
		const double d = kDefensive;
		if (local)
		{
			for (int i = 0; i < out.count; ++i)
			{
				out.geo[i] = estimate[i] > 0.0 ? (1.0 - d) * estimate[i] / total + d / active : 0.0;
				out.total_geo += out.geo[i];
			}
			out.env = env ? (1.0 - d) * env_e / total + d / active : 0.0;
		}
		else
		{
			// Split between geometry (static power) and environment 50/50 when both exist.
			out.total_geo = env ? 0.5 : 1.0;
			out.env = env ? 0.5 : 0.0;
		}
		return out;
	}

	double geometry_probability(const Probabilities& probs, int i) const
	{
		if (probs.count <= kMaxLocal)
			return probs.geo[static_cast<size_t>(i)];
		return probs.total_geo * global_geo[static_cast<size_t>(i)];
	}

	// Sample a direction from the light mixture. Returns false if no strategy is available.
	bool sample(const Probabilities& probs, const Point3& p, Vector3& dir) const
	{
		if (!probs.any)
			return false;
		double u = random_double();
		if (env && u < probs.env)
		{
			dir = env->random();
			return true;
		}
		u -= probs.env;
		for (int i = 0; i < probs.count; ++i)
		{
			const double pi_ = geometry_probability(probs, i);
			if (u < pi_ || i == probs.count - 1)
			{
				if (pi_ <= 0.0)
					break;
				dir = entries[static_cast<size_t>(i)].object->random(p);
				return true;
			}
			u -= pi_;
		}
		// Numerical leftover: fall back to the environment or the last active light.
		if (env && probs.env > 0.0)
		{
			dir = env->random();
			return true;
		}
		for (int i = probs.count - 1; i >= 0; --i)
		{
			if (geometry_probability(probs, i) > 0.0)
			{
				dir = entries[static_cast<size_t>(i)].object->random(p);
				return true;
			}
		}
		return false;
	}

	// Mixture density sum_i P_i p_i(dir) (solid angle).
	double pdf(const Probabilities& probs, const Point3& p, const Vector3& dir,
		Environment::LookupCache* environment_cache = nullptr) const
	{
		if (!probs.any)
			return 0.0;
		double value = 0.0;
		if (env && probs.env > 0.0)
			value += probs.env * (environment_cache ? env->pdf_value(dir, *environment_cache) : env->pdf_value(dir));
		for (int i = 0; i < probs.count; ++i)
		{
			const double pi_ = geometry_probability(probs, i);
			if (pi_ > 0.0)
				value += pi_ * entries[static_cast<size_t>(i)].object->pdf_value(p, dir);
		}
		return value;
	}

private:
	struct Entry
	{
		const Hittable* object = nullptr;
		LightShapeInfo info;
		bool known_emission = true;
	};

	std::vector<Entry> entries;
	std::vector<double> global_geo;
	const Environment* env = nullptr;
	double env_estimate = 0.0;

	static double geometry_estimate(const Entry& e, const Point3& p)
	{
		const Vector3 to_p = p - e.info.center;
		const double d2 = std::max(to_p.length_squared(), 1e-12);
		double omega = 0.0;
		if (e.info.shape == LightShapeInfo::Shape::Sphere)
		{
			const double r2 = e.info.radius * e.info.radius;
			if (d2 <= r2)
				return 0.0; // inside a (one-sided) sphere emitter: nothing reaches p
			omega = 2.0 * pi * (1.0 - std::sqrt(1.0 - r2 / d2));
		}
		else
		{
			const double d = std::sqrt(d2);
			double cos_l = dot(e.info.normal, to_p) / d;
			if (e.known_emission)
			{
				// One-sided emitter: every point of the light is seen from behind -> exactly 0.
				if (dot(e.info.normal, to_p) <= 0.0)
					return 0.0;
			}
			else
			{
				cos_l = std::abs(cos_l); // proxy: orientation unknown, never exclude
			}
			// Floor keeps grazing configurations (large light, point near its plane) sampleable.
			omega = std::min(2.0 * pi, e.info.area * std::max(cos_l, 0.05) / d2);
		}
		return e.info.luminance * omega;
	}
};
