#pragma once
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <random>

#include "Color.h"
#include "Ray.h"
#include "Vector3.h"

using std::make_shared;
using std::shared_ptr;

const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

inline double degrees_to_radians(double degrees)
{
    return degrees * pi / 180.0;
}

inline double random_double()
{
    static std::uniform_real_distribution<double> distribution(0.0, 1.0);
    static std::mt19937 generator;
    return distribution(generator);
}
inline double random_double(double min, double max)
{
    // Returns a random real in [min,max).
    return min + (max - min) * random_double();
}

inline Vector3 random()
{
    return Vector3(random_double(), random_double(), random_double());
}

inline Vector3 random(double min, double max)
{
    return Vector3(random_double(min, max), random_double(min, max), random_double(min, max));
}

inline int random_int(int min, int max)
{
	return static_cast<int>(random_double(min, max + 1));
}

inline Vector3 random_unit_vector()
{
	while (true)
	{
		auto p = Vector3(random(-1, 1));
		auto length = p.length_squared();
		if (1e-160 < length && length <= 1)
			return p / std::sqrt(length);
	}
}

inline Vector3 random_on_hemisphere(const Vector3 normal)
{
	auto on_unit_hemishere = random_unit_vector();
	if (dot(normal, on_unit_hemishere) > 0.0)
		return on_unit_hemishere;      // On the same side
	else
		return -on_unit_hemishere;
}

inline Vector3 reflect(const Vector3& v, const Vector3& n)
{
	return v - 2 * dot(v, n) * n;
}

inline Vector3 refract(const Vector3& uv, const Vector3& n, double etai_over_etat)
{
	auto cos_theta = std::fmin(dot(-uv, n), 1.0);
	auto r_out_perp = etai_over_etat * (uv + cos_theta * n);
	auto r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.length_squared())) * n;
	return r_out_perp + r_out_parallel;
}