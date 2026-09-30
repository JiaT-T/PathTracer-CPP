#pragma once
#include <vector>
#include <numeric>   // for std::iota
#include <algorithm> // for std::swap

#include "My_Common.h"

class Perlin
{
public :
	Perlin()
	{
		for (int i = 0; i < point_count; i++)
			random_vector[i] = normalize(random(-1, 1));

		generate_permutation(perm_x);
		generate_permutation(perm_y);
		generate_permutation(perm_z);
	}

	double noise(const Point3& p) const
	{
		// Get fractional part
		auto u = p.x() - std::floor(p.x());
		auto v = p.y() - std::floor(p.y());
		auto w = p.z() - std::floor(p.z());

		auto i = static_cast<int>(std::floor(p.x()));
		auto j = static_cast<int>(std::floor(p.y()));
		auto k = static_cast<int>(std::floor(p.z()));
		Vector3 c[2][2][2];

		for (int di = 0; di < 2; di++)
		{
			for (int dj = 0; dj < 2; dj++)
			{
				for (int dk = 0; dk < 2; dk++)
				{
					// XOR the three hashed coordinates to get a new random value from the random_float array
					c[di][dj][dk] = random_vector[perm_x[(i + di) & 255] ^ perm_y[(j + dj) & 255] ^ perm_z[(k + dk) & 255]];
				}
			}
		}
		return perlin_interp(c, u, v, w);
	}

	//
	// Depth is the number of octaves, 
	// which determines the frequency and amplitude of each layer of noise
	// 
	double turb(const Point3& p, int depth) const
	{
		auto accum = 0.0;
		auto temp_p = p;
		auto weight = 1.0;

		for (int i = 0; i < depth; i++)
		{
			accum += weight * noise(temp_p); // Add noise effects layer by layer, with each layer contributing less to the final result
			temp_p *= 2;                     // Double the frequency for the next octave, which means the noise pattern will be more detailed
			weight *= 0.5;                   // Halve the amplitude for the next octave, which means the contribution of the noise will be less significant
		}

		// The absolute value is taken to ensure that 
		// the turbulence effect is always positive, 
		// which can create more visually interesting patterns
		return std::fabs(accum);
	}

private :
	static const int point_count = 256;
	Vector3 random_vector[point_count];
	std::vector<int> perm_x;
	std::vector<int> perm_y;
	std::vector<int> perm_z;

    void generate_permutation(std::vector<int>& p)
    {
        p.resize(point_count);
        std::iota(p.begin(), p.end(), 0);

        // Fisher-Yates driven by the renderer RNG, so the noise pattern is reproducible
        // for a given scene seed (std::random_device made every run different).
        for (int i = point_count - 1; i > 0; i--)
        {
            const int target = random_int(0, i);
            std::swap(p[i], p[target]);
        }
    }

	static double perlin_interp(const Vector3 c[2][2][2], double u, double v, double w)
	{

		// Hermite Smoothing
		auto uu = u * u * (3 - 2 * u);
		auto vv = v * v * (3 - 2 * v);
		auto ww = w * w * (3 - 2 * w);
		auto accum = 0.0;

		for (int i = 0; i < 2; i++)
		{
			for (int j = 0; j < 2; j++)
			{
				for (int k = 0; k < 2; k++)
				{
					Vector3 weight = Vector3(u - i, v - j, w - k);
					accum += (i * uu + (1 - i) * (1 - uu)) *
							 (j * vv + (1 - j) * (1 - vv)) *
							 (k * ww + (1 - k) * (1 - ww)) *
							 dot(weight, c[i][j][k]);
				}
			}
		}
		return accum;
	}
};

