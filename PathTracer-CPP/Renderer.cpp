#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "My_Common.h"
#include "Camera.h"
#include "Scenes.h"
#include "Timer.h"
#include "PPMPreviewWindow.h"
#include "Tests.h"
#include "Benchmark.h"

namespace
{
	struct CliOptions
	{
		std::string scene = "readme_showcase";
		int spp = -1;
		int width = -1;
		int depth = -1;
		int threads = 0;
		bool has_seed = false;
		uint64_t seed = 0;
		std::string out;
		std::string output_mode;
		bool preview = true;
		bool keep_preview_open = false;
	};

	void print_usage()
	{
		std::cout <<
			"Usage: PathTracer-CPP.exe [options]\n"
			"\n"
			"Rendering:\n"
			"  --scene <name|id>       Scene to render (default: readme_showcase). See --list.\n"
			"  --spp <n>               Samples per pixel (overrides the scene default)\n"
			"  --width <px>            Image width; height follows the scene aspect ratio\n"
			"  --depth <n>             Maximum path depth\n"
			"  --seed <n>              Fixed RNG seed (default: random, printed at start)\n"
			"  --threads <n>           Worker threads (0 = all hardware threads)\n"
			"  --out <file.ppm>        Output file; a linear <file>.pfm is written next to it\n"
			"  --output-mode <m>       raw | denoised | both\n"
			"  --no-preview            Do not open the Win32 preview window\n"
			"  --keep-preview          Keep the preview window open after rendering\n"
			"  --list                  List scenes\n"
			"  --image-stats <file>    Per-channel statistics of an image's encoded values\n"
			"\n"
			"Validation:\n"
			"  --test [filter]         Numerical unit tests (PDF, BSDF, furnace, determinism)\n"
			"  --regress [--update-references] [--ref-spp n]\n"
			"                          Render small fixed-seed scenes and compare to tests/reference\n"
			"  --bench [--scene s] [--spp n] [--width px] [--threads-list 1,2,4,0] [--repeat n]\n"
			"                          Repeatable benchmark with ray counters and thread scaling\n";
	}

	uint64_t random_seed()
	{
		std::random_device rd;
		const uint64_t hi = static_cast<uint64_t>(rd()) << 32;
		const uint64_t lo = static_cast<uint64_t>(rd());
		const uint64_t t = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
		return (hi | lo) ^ rng::splitmix64(t);
	}

	// Per-channel statistics of the raw encoded values of an image (no color-space decoding),
	// used for asset review (e.g. is a roughness map really roughness, or glossiness?).
	int image_stats_command(const std::string& path)
	{
		const rtw_image image(path.c_str());
		if (!image.is_valid())
			return 2;
		const int w = image.width();
		const int h = image.height();
		std::vector<std::vector<double>> channels(3);
		for (auto& c : channels) c.reserve(static_cast<size_t>(w) * h);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const Color c = image.float_pixel(x, y);
				for (int k = 0; k < 3; ++k) channels[k].push_back(c[k]);
			}
		std::cout << path << "  " << w << "x" << h << "\n";
		const char* names[3] = { "R", "G", "B" };
		for (int k = 0; k < 3; ++k)
		{
			auto& v = channels[k];
			double sum = 0.0;
			for (double x : v) sum += x;
			std::sort(v.begin(), v.end());
			auto pct = [&](double p) { return v[static_cast<size_t>(p * (v.size() - 1))]; };
			std::cout << "  " << names[k] << ": mean " << sum / v.size() << "  min " << v.front()
				<< "  p5 " << pct(0.05) << "  p50 " << pct(0.5) << "  p95 " << pct(0.95) << "  max " << v.back() << "\n";
		}
		return 0;
	}

	void list_scenes()
	{
		for (const SceneEntry& entry : scene_registry())
		{
			std::cout << "  " << entry.id << "\t" << entry.name << (entry.test_scene ? "  [test]" : "")
				<< "\n\t" << entry.description << "\n";
		}
	}

	int render_command(const CliOptions& options)
	{
		const SceneEntry* entry = find_scene(options.scene);
		if (!entry)
		{
			std::cerr << "Unknown scene '" << options.scene << "'. Use --list.\n";
			return 2;
		}

		const uint64_t seed = options.has_seed ? options.seed : random_seed();
		// Scene construction (random layouts, Perlin tables) uses the same seed.
		rng::seed_thread(seed);
		SceneDesc scene = entry->build();
		Camera& cam = scene.cam;
		cam.seed = seed;
		cam.thread_count = options.threads;
		if (options.spp > 0) cam.sample_per_pixel = options.spp;
		if (options.width > 0) cam.image_width = options.width;
		if (options.depth > 0) cam.max_depth = options.depth;
		if (!options.out.empty()) cam.output_filename = options.out;
		if (options.output_mode == "raw") cam.progressive_output_mode = Camera::Progressive_Output_Mode::Raw;
		else if (options.output_mode == "denoised") cam.progressive_output_mode = Camera::Progressive_Output_Mode::Denoised;
		else if (options.output_mode == "both") cam.progressive_output_mode = Camera::Progressive_Output_Mode::Denoised_With_Raw;

		std::clog << "Scene: " << scene.name
			<< "  " << cam.image_width << "x" << cam.output_height()
			<< "  spp " << cam.sample_per_pixel
			<< "  depth " << cam.max_depth
			<< "  seed " << seed
			<< "  threads " << (options.threads > 0 ? std::to_string(options.threads) : std::string("all"))
			<< "\n";

		if (scene.use_lights)
		{
			// Debug output for the light-selection heuristic, evaluated at the camera target.
			const Vector3 up(0, 1, 0);
			const auto probs = cam.DescribeLightSelection(scene.lights, cam.lookat, &up);
			std::clog << "Light selection at lookat (normal +Y): environment " << probs.env
				<< ", geometry lights " << probs.total_geo
				<< " (" << probs.count << " light" << (probs.count == 1 ? "" : "s") << ")\n";
		}

		std::unique_ptr<PPMPreviewWindow> preview;
		if (options.preview)
			preview = std::make_unique<PPMPreviewWindow>(cam.output_filename, cam.image_width, cam.output_height());

		Timer timer;
		if (scene.use_lights)
			cam.RenderProgressive(scene.world, scene.lights, preview.get());
		else
			cam.RenderProgressive(scene.world, preview.get());
		const double seconds = timer.stop();

		if (preview)
		{
			preview->SetFinished(seconds);
			if (options.keep_preview_open)
				preview->WaitUntilClosed();
		}
		std::clog << "Wrote " << cam.output_filename << " (+ linear .pfm)\n";
		return 0;
	}
}

int main(int argc, char** argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);
	CliOptions options;

	enum class Command { Render, Test, Regress, Bench, List, Help } command = Command::Render;
	std::string test_filter;
	RegressionOptions regress;
	BenchmarkOptions bench;

	for (size_t i = 0; i < args.size(); ++i)
	{
		const std::string& a = args[i];
		auto next = [&](const char* name) -> std::string
		{
			if (i + 1 >= args.size())
			{
				std::cerr << "Missing value for " << name << "\n";
				std::exit(2);
			}
			return args[++i];
		};

		if (a == "--help" || a == "-h") command = Command::Help;
		else if (a == "--list") command = Command::List;
		else if (a == "--image-stats") return image_stats_command(next("--image-stats"));
		else if (a == "--test")
		{
			command = Command::Test;
			if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0)
				test_filter = args[++i];
		}
		else if (a == "--regress") command = Command::Regress;
		else if (a == "--update-references") regress.update_references = true;
		else if (a == "--ref-spp") regress.reference_spp = std::stoi(next("--ref-spp"));
		else if (a == "--bench") command = Command::Bench;
		else if (a == "--threads-list") bench.thread_list = parse_int_list(next("--threads-list"));
		else if (a == "--repeat") bench.repeat = std::stoi(next("--repeat"));
		else if (a == "--scene") { options.scene = next("--scene"); bench.scene = options.scene; }
		else if (a == "--spp") { options.spp = std::stoi(next("--spp")); bench.spp = options.spp; }
		else if (a == "--width") { options.width = std::stoi(next("--width")); bench.width = options.width; }
		else if (a == "--depth") { options.depth = std::stoi(next("--depth")); bench.depth = options.depth; }
		else if (a == "--seed") { options.has_seed = true; options.seed = std::stoull(next("--seed")); bench.seed = options.seed; }
		else if (a == "--threads") options.threads = std::stoi(next("--threads"));
		else if (a == "--out") options.out = next("--out");
		else if (a == "--output-mode") options.output_mode = next("--output-mode");
		else if (a == "--no-preview") options.preview = false;
		else if (a == "--keep-preview") options.keep_preview_open = true;
		else
		{
			std::cerr << "Unknown argument '" << a << "'\n";
			print_usage();
			return 2;
		}
	}

	switch (command)
	{
	case Command::Help: print_usage(); return 0;
	case Command::List: list_scenes(); return 0;
	case Command::Test: return run_unit_tests(test_filter);
	case Command::Regress: return run_regression(regress);
	case Command::Bench: return run_benchmark(bench);
	case Command::Render: break;
	}
	return render_command(options);
}
