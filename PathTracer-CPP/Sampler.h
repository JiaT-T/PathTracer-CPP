#pragma once
#include <cstdint>

// Deterministic random number generation.
//
// Every random decision in the renderer goes through random_double() (My_Common.h), which
// reads a thread_local PCG32 generator. The renderer re-seeds that generator at the start of
// every (pixel, sample) pair from (global_seed, pixel_index, sample_index), so a pixel sample
// consumes exactly the same random sequence no matter which thread renders it, in which order
// tiles are scheduled, or how samples are grouped into progressive passes. As a result, the
// same seed + scene + resolution + spp + depth produces bit-identical images for any thread count.
//
// Scene construction (random sphere layouts, Perlin permutations) runs on the main thread after
// rng::seed_thread(scene_seed), so procedural scene content is reproducible as well.
namespace rng
{
	inline uint64_t splitmix64(uint64_t x)
	{
		x += 0x9E3779B97F4A7C15ull;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
		return x ^ (x >> 31);
	}

	// PCG32 (O'Neill 2014, pcg32_random_r): 64-bit state, selectable stream, 32-bit output.
	struct PCG32
	{
		uint64_t state = 0x853c49e6748fea9bull;
		uint64_t inc = 0xda3e39cb94b95bdbull;

		void seed(uint64_t init_state, uint64_t init_sequence)
		{
			state = 0u;
			inc = (init_sequence << 1u) | 1u;
			next_u32();
			state += init_state;
			next_u32();
		}

		uint32_t next_u32()
		{
			const uint64_t old_state = state;
			state = old_state * 6364136223846793005ull + inc;
			const uint32_t xorshifted = static_cast<uint32_t>(((old_state >> 18u) ^ old_state) >> 27u);
			const uint32_t rot = static_cast<uint32_t>(old_state >> 59u);
			return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
		}

		// Uniform double in [0, 1) with 32 bits of resolution.
		double next_double()
		{
			return static_cast<double>(next_u32()) * (1.0 / 4294967296.0);
		}
	};

	inline PCG32& thread_generator()
	{
		thread_local PCG32 generator;
		return generator;
	}

	// Seed the calling thread's generator (used for scene construction and tests).
	inline void seed_thread(uint64_t seed)
	{
		const uint64_t h = splitmix64(seed);
		thread_generator().seed(splitmix64(h ^ 0x6A09E667F3BCC909ull), h);
	}

	// Start the independent random stream of one pixel sample.
	inline void begin_pixel_sample(uint64_t global_seed, uint64_t pixel_index, uint64_t sample_index)
	{
		const uint64_t pixel_hash = splitmix64(global_seed ^ splitmix64(pixel_index + 0x9E3779B97F4A7C15ull));
		const uint64_t sample_hash = splitmix64(pixel_hash ^ splitmix64(sample_index));
		thread_generator().seed(sample_hash, pixel_hash ^ (sample_index << 1));
	}

	// Kensler 2013 ("Correlated Multi-Jittered Sampling"): hash-based permutation of [0, length).
	// Used to visit the stratified sub-pixel cells of a pixel in a per-pixel random order, so
	// progressive intermediate results are not biased toward the first rows of strata.
	inline uint32_t permute(uint32_t i, uint32_t length, uint32_t seed)
	{
		if (length <= 1)
			return 0;
		uint32_t w = length - 1;
		w |= w >> 1; w |= w >> 2; w |= w >> 4; w |= w >> 8; w |= w >> 16;
		do
		{
			i ^= seed;
			i *= 0xe170893du;
			i ^= seed >> 16;
			i ^= (i & w) >> 4;
			i ^= seed >> 8;
			i *= 0x0929eb3fu;
			i ^= seed >> 23;
			i ^= (i & w) >> 1;
			i *= 1u | seed >> 27;
			i *= 0x6935fa69u;
			i ^= (i & w) >> 11;
			i *= 0x74dcb303u;
			i ^= (i & w) >> 2;
			i *= 0x9e501cc3u;
			i ^= (i & w) >> 2;
			i *= 0xc860a3dfu;
			i &= w;
			i ^= i >> 5;
		} while (i >= length);
		return (i + seed) % length;
	}
}
