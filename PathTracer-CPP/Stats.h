#pragma once
#include <cstdint>

// Lightweight per-thread render counters.
//
// Each worker increments its own thread_local copy (no atomics, no false sharing); the render
// loop flushes them into a per-render total after every tile. PT_TRAVERSAL_STATS controls the
// counters inside BVH / primitive intersection (one increment per node / primitive test).
#ifndef PT_TRAVERSAL_STATS
#define PT_TRAVERSAL_STATS 1
#endif

struct RenderCounters
{
	uint64_t camera_rays = 0;      // primary rays
	uint64_t bounce_rays = 0;      // path continuation rays
	uint64_t shadow_rays = 0;      // NEE visibility rays
	uint64_t path_vertices = 0;    // surface / medium interactions
	uint64_t bvh_nodes = 0;        // BVH nodes visited (= ray-AABB tests)
	uint64_t primitive_tests = 0;  // ray-primitive intersection tests

	uint64_t total_rays() const { return camera_rays + bounce_rays + shadow_rays; }

	RenderCounters& operator+=(const RenderCounters& o)
	{
		camera_rays += o.camera_rays;
		bounce_rays += o.bounce_rays;
		shadow_rays += o.shadow_rays;
		path_vertices += o.path_vertices;
		bvh_nodes += o.bvh_nodes;
		primitive_tests += o.primitive_tests;
		return *this;
	}
};

// Number of global operator new calls since program start (Stats.cpp).
uint64_t heap_allocation_count();

inline RenderCounters& thread_counters()
{
	thread_local RenderCounters counters;
	return counters;
}

#if PT_TRAVERSAL_STATS
#define PT_COUNT_BVH_NODE() (++thread_counters().bvh_nodes)
#define PT_COUNT_PRIMITIVE() (++thread_counters().primitive_tests)
#else
#define PT_COUNT_BVH_NODE() ((void)0)
#define PT_COUNT_PRIMITIVE() ((void)0)
#endif
