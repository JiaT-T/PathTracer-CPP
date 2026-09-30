#include "Benchmark.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "Scenes.h"
#include "Parallel.h"
#include "Stats.h"

namespace
{
	std::string cpu_brand()
	{
#ifdef _MSC_VER
		int regs[4] = {};
		char brand[49] = {};
		__cpuid(regs, 0x80000000);
		if (static_cast<unsigned>(regs[0]) >= 0x80000004u)
		{
			for (int i = 0; i < 3; ++i)
			{
				__cpuid(regs, 0x80000002 + i);
				std::memcpy(brand + 16 * i, regs, sizeof(regs));
			}
			std::string s(brand);
			const size_t first = s.find_first_not_of(' ');
			return first == std::string::npos ? s : s.substr(first);
		}
#endif
		return "unknown";
	}

	std::string compiler_string()
	{
#if defined(_MSC_FULL_VER)
		return "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
		return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
		return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
		return "unknown";
#endif
	}

	std::string build_mode()
	{
#ifdef NDEBUG
		return "Release";
#else
		return "Debug";
#endif
	}
}

int run_benchmark(const BenchmarkOptions& options)
{
	const SceneEntry* entry = find_scene(options.scene);
	if (!entry)
	{
		std::cerr << "Unknown benchmark scene '" << options.scene << "'\n";
		return 2;
	}

	rng::seed_thread(options.seed);
	SceneDesc scene = entry->build();
	Camera& cam = scene.cam;
	if (options.spp > 0) cam.sample_per_pixel = options.spp;
	if (options.width > 0) cam.image_width = options.width;
	if (options.depth > 0) cam.max_depth = options.depth;
	cam.seed = options.seed;
	cam.write_outputs = false;
	cam.verbose = false;
	cam.render_mode = Camera::Render_Mode::Parallel;

	const int width = cam.image_width;
	const int height = cam.output_height();
	const double total_samples = static_cast<double>(width) * height * cam.sample_per_pixel;

	std::cout << "## Benchmark\n\n"
		<< "| Key | Value |\n|---|---|\n"
		<< "| Scene | " << scene.name << " |\n"
		<< "| Resolution | " << width << "x" << height << " |\n"
		<< "| SPP | " << cam.sample_per_pixel << " |\n"
		<< "| Max depth | " << cam.max_depth << " |\n"
		<< "| Seed | " << options.seed << " |\n"
		<< "| CPU | " << cpu_brand() << " (" << parallel::hardware_threads() << " hardware threads) |\n"
		<< "| Compiler | " << compiler_string() << " |\n"
		<< "| Build | " << build_mode() << " x64 |\n"
		<< "| Repeat | best of " << std::max(1, options.repeat) << " |\n\n";

	std::cout << "| Threads | Time (s) | Samples/s | MRays/s | Speedup | Efficiency |\n"
		<< "|---:|---:|---:|---:|---:|---:|\n" << std::flush;

	double single_thread_seconds = 0.0;
	RenderCounters counters;
	uint64_t allocations_during_render = 0;
	for (int requested : options.thread_list)
	{
		const int threads = parallel::resolve_thread_count(requested);
		cam.thread_count = threads;

		double best = 1e300;
		for (int r = 0; r < std::max(1, options.repeat); ++r)
		{
			const uint64_t alloc_before = heap_allocation_count();
			const auto start = std::chrono::steady_clock::now();
			if (scene.use_lights)
				cam.Render(scene.world, scene.lights);
			else
				cam.Render(scene.world);
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			allocations_during_render = heap_allocation_count() - alloc_before;
			best = std::min(best, seconds);
		}
		counters = cam.LastCounters(); // identical for every thread count (deterministic)

		if (threads == 1)
			single_thread_seconds = best;
		const double speedup = single_thread_seconds > 0.0 ? single_thread_seconds / best : 0.0;
		std::cout << "| " << threads
			<< " | " << std::fixed << std::setprecision(3) << best
			<< " | " << std::setprecision(0) << total_samples / best
			<< " | " << std::setprecision(2) << counters.total_rays() / best * 1e-6
			<< " | " << std::setprecision(2) << (speedup > 0 ? speedup : 0.0) << "x"
			<< " | " << std::setprecision(1) << (speedup > 0 ? 100.0 * speedup / threads : 0.0) << "% |\n"
			<< std::defaultfloat << std::flush;
	}

	const double paths = static_cast<double>(std::max<uint64_t>(1, counters.camera_rays));
	const double rays = static_cast<double>(std::max<uint64_t>(1, counters.total_rays()));
	std::cout << "\n| Counter | Total | Per camera path |\n|---|---:|---:|\n" << std::fixed << std::setprecision(3)
		<< "| Primary rays | " << counters.camera_rays << " | 1 |\n"
		<< "| Continuation rays | " << counters.bounce_rays << " | " << counters.bounce_rays / paths << " |\n"
		<< "| Shadow rays | " << counters.shadow_rays << " | " << counters.shadow_rays / paths << " |\n"
		<< "| Total rays | " << counters.total_rays() << " | " << counters.total_rays() / paths << " |\n"
		<< "| Scattering vertices (avg path length) | " << counters.path_vertices << " | " << counters.path_vertices / paths << " |\n"
		<< "| BVH node visits (ray-AABB tests) | " << counters.bvh_nodes << " | " << counters.bvh_nodes / rays << " per ray |\n"
		<< "| Primitive tests | " << counters.primitive_tests << " | " << counters.primitive_tests / rays << " per ray |\n"
		<< "| Heap allocations during last render | " << allocations_during_render << " | "
		<< allocations_during_render / paths << " |\n" << std::defaultfloat;
	return 0;
}
