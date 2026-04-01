#pragma once
#include <vector>
#include <numeric>   // for std::iota
#include <random>    // for modern random number generation
#include <algorithm> // for std::shuffle

#include "My_Common.h"

class Perlin
{
public :
	Perlin()
	{
		for (int i = 0; i < point_count; i++)
			random_float[i] = random_double();

		generate_permutation(perm_x);
		generate_permutation(perm_y);
		generate_permutation(perm_z);
	}

	double noise(const Point3& p) const
	{
		auto i = static_cast<int>(std::floor(FREQUENCY * p.x())) & INDEX_MASK;
		auto j = static_cast<int>(std::floor(FREQUENCY * p.y())) & INDEX_MASK;
		auto k = static_cast<int>(std::floor(FREQUENCY * p.z())) & INDEX_MASK;

		// XOR the three hashed coordinates to get a new random value from the random_float array
		return random_float[perm_x[i] ^ perm_y[j] ^ perm_z[k]];
	}

private :
	static const int point_count = 256;
	double random_float[point_count];
	std::vector<int> perm_x;
	std::vector<int> perm_y;
	std::vector<int> perm_z;

	static constexpr int INDEX_MASK = 255;
	static constexpr double FREQUENCY = 4.0;

    void generate_permutation(std::vector<int>& p)
    {
        p.resize(point_count);

        std::numeric_limits<int> max_val;
        std::iota(p.begin(), p.end(), 0);

        std::random_device rd;
        std::mt19937 gen(rd());

        std::shuffle(p.begin(), p.end(), gen);
    }
};

