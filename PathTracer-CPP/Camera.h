#pragma once
#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "Hittable.h"
#include "Hittable_List.h"
#include "My_Common.h"
#include "Material.h"
#include "RenderPreview.h"
#include "Environment.h"
#include "PostProcess.h"
#include "ImageIO.h"
#include "Parallel.h"
#include "LightSampler.h"
#include "Integrator.h"
#include "RenderAOV.h"
#include "Stats.h"

// Camera model + render loop. Radiance estimation lives in PathIntegrator (Integrator.h).
class Camera
{
public : 
	enum class Render_Mode
	{
		Serial,
		Parallel
	};

	enum class Progressive_Output_Mode
	{
		Raw,
		Denoised,
		Denoised_With_Raw
	};

	// Image
	double aspect_ratio     = 16.0 / 9.0;
	int    image_width      = 400;
	int    sample_per_pixel = 100;
	int    max_depth        = 10;
	Color  background;
	std::string output_filename = "image.ppm";
	Render_Mode render_mode = Render_Mode::Parallel;
	Progressive_Output_Mode progressive_output_mode = Progressive_Output_Mode::Denoised;

	// Integrator
	int first_bounce_light_samples = 4;
	bool russian_roulette = true;
	int rr_start_bounce = 3;

	// Debug AOVs written next to the output (<name>_<aov>.pfm / .ppm), see RenderAOV.h
	std::vector<AOVKind> aovs;
	AtrousDenoiser::Settings denoiser_settings;
	DisplaySettings display_settings; // Presentation only; never affects accumulation / guides.
	bool preview_denoise = true;
	int preview_denoise_min_samples = 8;
	double preview_denoise_interval_seconds = 0.75;

	// Reproducibility / execution
	uint64_t seed = 1;          // Global seed; every (pixel, sample) stream is derived from it
	int thread_count = 0;       // 0 = all hardware threads; ignored when render_mode == Serial
	bool write_outputs = true;  // false: keep results in memory only (tests / benchmarks)
	bool write_linear_output = true; // raw linear accumulation -> <output>.pfm
	bool verbose = true;

	double  vfov     = 90;
	Vector3 lookfrom = Point3(0, 0, 0);
	Vector3 lookat   = Point3(0, 0, -1);
	Vector3 up       = Vector3(0, 1, 0);

	double defocus_angle = 0;  // Variation angle of rays through each pixel
	double focus_dist = 10;    // Distance from camera lookfrom point to plane of perfect focus

	int output_height() const
	{
		int height = static_cast<int>(image_width / aspect_ratio);
		return (height < 1) ? 1 : height;
	}

	void SetEnvironment(std::shared_ptr<Environment> env)
	{
		environment = std::move(env);
	}

	const std::shared_ptr<Environment>& GetEnvironment() const { return environment; }

	// Debug: light-selection probabilities (environment / total geometry) at point p.
	// normal = nullptr evaluates the volume-scattering case.
	LightSampler::Probabilities DescribeLightSelection(const Hittable_List& lights, const Point3& p, const Vector3* normal) const
	{
		LightSampler sampler;
		sampler.build(&lights, environment.get());
		return sampler.probabilities(p, normal);
	}

	// Results of the most recent render (linear HDR, row-major, top row first).
	const std::vector<Color>& LastFramebuffer() const { return framebuffer; }
	const std::vector<Color>& LastDenoised() const { return filtered_framebuffer; }
	const std::vector<PixelGuide>& LastGuide() const { return guide_buffer; }
	uint64_t LastNonFiniteSamples() const { return last_non_finite_samples; }
	int LastSampleCount() const { return last_sample_count; }
	const RenderCounters& LastCounters() const { return last_counters; }
	// Linear AOV of the most recent render (requested AOVs, plus Variance / SampleCount always).
	std::vector<Color> LastAOV(AOVKind kind) const { return aov_buffers.resolve(kind); }

	// Denoise the most recent render with explicit settings (evaluation / comparisons).
	std::vector<Color> Denoise(const AtrousDenoiser::Settings& settings) const
	{
		std::vector<Color> out;
		PostProcessInput input{ framebuffer, guide_buffer, image_width, image_height };
		PostProcessOutput output{ out };
		auto bounded_settings = settings;
		bounded_settings.thread_count = resolved_thread_count();
		AtrousDenoiser(bounded_settings).Apply(input, output);
		return out;
	}

	// Ver.1: no explicit light list (pure BSDF sampling; emission found by chance).
	void Render(const Hittable& world, RenderPreview* preview = nullptr)
	{
		light_sampler.build(nullptr, nullptr);
		render_image(world, preview, false);
	}

	// Ver.2: next-event estimation towards `lights` (+ environment) with MIS.
	void Render(const Hittable& world, const Hittable_List& lights, RenderPreview* preview = nullptr)
	{
		light_sampler.build(&lights, environment.get());
		render_image(world, preview, false);
	}

	void RenderProgressive(const Hittable& world, RenderPreview* preview = nullptr)
	{
		light_sampler.build(nullptr, nullptr);
		render_image(world, preview, true);
	}

	void RenderProgressive(const Hittable& world, const Hittable_List& lights, RenderPreview* preview = nullptr)
	{
		light_sampler.build(&lights, environment.get());
		render_image(world, preview, true);
	}

private :
	struct RenderTile
	{
		int x_begin = 0;
		int x_end = 0;
		int y_begin = 0;
		int y_end = 0;
	};

	static constexpr int kTileSize = 8;

	std::vector<Color> accumulation;
	std::vector<Color> framebuffer;
	std::vector<Color> filtered_framebuffer;
	std::vector<PixelGuide> guide_buffer;
	AOVBuffers aov_buffers;
	uint64_t last_non_finite_samples = 0;
	int last_sample_count = 0;
	RenderCounters last_counters;

	int resolved_thread_count() const
	{
		return render_mode == Render_Mode::Serial ? 1 : parallel::resolve_thread_count(thread_count);
	}

	PathIntegrator make_integrator(const Hittable& world) const
	{
		PathIntegrator::Settings settings;
		settings.max_depth = max_depth;
		settings.first_bounce_light_samples = first_bounce_light_samples;
		settings.russian_roulette = russian_roulette;
		settings.rr_start_bounce = rr_start_bounce;
		return PathIntegrator(world, light_sampler, environment.get(), background, settings);
	}

	// Shared render loop.
	//
	// Every pixel sample starts its own RNG stream (rng::begin_pixel_sample), and each pixel is
	// accumulated by exactly one thread in sample order, so the result is bit-identical for any
	// thread count, tile schedule, or progressive pass grouping.
	//
	// progressive = true: several passes (preview refresh between passes), then the denoiser.
	// progressive = false: one pass with all samples, no denoising.
	void render_image(const Hittable& world, RenderPreview* preview, bool progressive)
	{
		initialize();
		const PathIntegrator integrator = make_integrator(world);

		const bool writes_denoised_output =
			progressive && progressive_output_mode != Progressive_Output_Mode::Raw;
		const bool writes_raw_sidecar =
			progressive && progressive_output_mode == Progressive_Output_Mode::Denoised_With_Raw;
		const std::string raw_output_filename = image_io::add_suffix(output_filename, "_raw");

		if (write_outputs)
		{
			if (!can_open_output_file(output_filename))
				return;
			if (writes_raw_sidecar && !can_open_output_file(raw_output_filename))
				return;
		}

		const size_t pixel_count = static_cast<size_t>(image_width) * image_height;
		accumulation.assign(pixel_count, Color(0, 0, 0));
		framebuffer.assign(pixel_count, Color(0, 0, 0));
		filtered_framebuffer.clear();
		guide_buffer.assign(pixel_count, PixelGuide{});
		aov_buffers.reset(pixel_count, aovs);
		const std::vector<Color> no_filtered_preview;
		std::vector<Color> interim_filtered;
		const std::vector<RenderTile> tiles = build_tiles();
		const int total_samples = std::max(1, sample_per_pixel);
		const int threads = resolved_thread_count();
		std::atomic<uint64_t> non_finite_samples{ 0 };
		RenderCounters counters_total;
		std::mutex counters_mutex;

		auto preview_start_time = std::chrono::steady_clock::now();
		auto last_preview_denoise = preview_start_time;
		bool has_denoised_preview = false;
		if (preview && !preview->IsClosed())
		{
			preview->Update({ framebuffer, no_filtered_preview, 0, total_samples, 0.0, false });
		}

		// With a live preview, start with 1 sample per pass and grow the batch until a pass
		// takes ~0.1 s. Without preview everything runs as a single pass.
		const bool show_passes = progressive && preview != nullptr;
		int batch = show_passes ? 1 : total_samples;
		int samples_done = 0;

		while (samples_done < total_samples)
		{
			const int s_begin = samples_done;
			const int s_end = std::min(total_samples, samples_done + batch);
			const auto pass_start = std::chrono::steady_clock::now();
			std::atomic<int> tiles_done{ 0 };
			std::atomic<int> last_percent{ -1 };
			std::mutex print_mutex;

			parallel::parallel_for(static_cast<int>(tiles.size()), threads, [&](int tile_index, int)
			{
				thread_counters() = RenderCounters{};
				const RenderTile& tile = tiles[static_cast<size_t>(tile_index)];
				for (int j = tile.y_begin; j < tile.y_end; j++)
				{
					for (int i = tile.x_begin; i < tile.x_end; i++)
					{
						const size_t index = static_cast<size_t>(j) * image_width + i;
						Color sum = accumulation[index];
						for (int s = s_begin; s < s_end; ++s)
						{
							rng::begin_pixel_sample(seed, index, static_cast<uint64_t>(s));
							PathSample record;
							const Color c = integrator.Li(get_ray(i, j, s), &record);
							if (!std::isfinite(c.x()) || !std::isfinite(c.y()) || !std::isfinite(c.z()))
							{
								// A single NaN/Inf would poison the pixel forever; drop it and count it.
								non_finite_samples.fetch_add(1, std::memory_order_relaxed);
								continue;
							}
							sum += c;
							aov_buffers.accumulate(index, record);
						}
						accumulation[index] = sum;
						framebuffer[index] = sum / static_cast<double>(s_end);
					}
				}

				{
					std::lock_guard<std::mutex> lock(counters_mutex);
					counters_total += thread_counters();
				}

				if (verbose && !show_passes)
				{
					const int done = tiles_done.fetch_add(1, std::memory_order_relaxed) + 1;
					const int percent = static_cast<int>(100.0 * done / tiles.size());
					int previous = last_percent.load(std::memory_order_relaxed);
					if (percent > previous && last_percent.compare_exchange_strong(previous, percent))
					{
						std::lock_guard<std::mutex> lock(print_mutex);
						std::clog << "\rProgress: " << percent << "% " << std::flush;
					}
				}
			});

			samples_done = s_end;
			const auto now = std::chrono::steady_clock::now();
			if (verbose && show_passes)
				std::clog << "\rSamples: " << samples_done << " / " << total_samples << ' ' << std::flush;

			if (preview && !preview->IsClosed())
			{
				// Publish raw immediately; retain the last filtered snapshot until a new one is ready.
				preview->Update({ framebuffer, no_filtered_preview, samples_done, total_samples,
					std::chrono::duration<double>(now - preview_start_time).count(), false });
				const double since_filter = std::chrono::duration<double>(now - last_preview_denoise).count();
				if (progressive && preview_denoise && denoiser_settings.enabled && !preview->IsClosed() &&
					samples_done < total_samples && samples_done >= std::max(1, preview_denoise_min_samples) &&
					(!has_denoised_preview || since_filter >= std::max(0.0, preview_denoise_interval_seconds)))
				{
					// All tracing workers have joined: color and guide describe exactly the same samples.
					// Filter at pass boundaries, sharing the render thread budget, with no concurrent writes.
					resolve_guides();
					auto settings = denoiser_settings;
					settings.iterations = std::min(settings.iterations, 2);
					settings.thread_count = threads;
					PostProcessInput input{ framebuffer, guide_buffer, image_width, image_height };
					PostProcessOutput output{ interim_filtered };
					AtrousDenoiser(settings).Apply(input, output);
					last_preview_denoise = std::chrono::steady_clock::now();
					has_denoised_preview = true;
					if (!preview->IsClosed())
						preview->Update({ framebuffer, interim_filtered, samples_done, total_samples,
							std::chrono::duration<double>(last_preview_denoise - preview_start_time).count(), false });
				}
			}

			if (show_passes)
			{
				const double pass_seconds = std::chrono::duration<double>(now - pass_start).count();
				if (pass_seconds < 0.1)
					batch = std::min(batch * 2, 64);
			}
		}

		last_non_finite_samples = non_finite_samples.load();
		last_sample_count = total_samples;
		last_counters = counters_total;
		if (verbose && last_non_finite_samples > 0)
			std::clog << "\nWarning: dropped " << last_non_finite_samples << " non-finite samples.\n";

		resolve_guides();

		const bool needs_filtered_framebuffer =
			writes_denoised_output || (progressive && preview_denoise && denoiser_settings.enabled && preview && !preview->IsClosed());
		if (needs_filtered_framebuffer)
		{
			filtered_framebuffer.assign(pixel_count, Color(0, 0, 0));
			PostProcessInput post_input{ framebuffer, guide_buffer, image_width, image_height };
			PostProcessOutput post_output{ filtered_framebuffer };
			auto settings = denoiser_settings;
			settings.thread_count = threads;
			const AtrousDenoiser denoiser(settings);
			denoiser.Apply(post_input, post_output);
		}

		if (preview && !preview->IsClosed())
		{
			std::chrono::duration<double> elapsed_seconds = std::chrono::steady_clock::now() - preview_start_time;
			preview->Update({ framebuffer, denoiser_settings.enabled ? filtered_framebuffer : no_filtered_preview,
				total_samples, total_samples, elapsed_seconds.count(), true });
		}
		// Freeze the presentation settings used for this export; later window edits can use S.
		const DisplaySettings output_display = preview ? preview->GetDisplaySettings() : display_settings;

		if (write_outputs)
		{
			const std::vector<Color>& display = writes_denoised_output ? filtered_framebuffer : framebuffer;
			if (writes_raw_sidecar)
				image_io::write_ppm(raw_output_filename, framebuffer, image_width, image_height, output_display);
			image_io::write_ppm(output_filename, display, image_width, image_height, output_display);
			if (write_linear_output)
			{
				// Linear, unfiltered estimate: the only output suitable for numerical comparison.
				image_io::write_pfm(image_io::replace_extension(output_filename, ".pfm"), framebuffer, image_width, image_height);
				if (writes_denoised_output)
					image_io::write_pfm(image_io::replace_extension(output_filename, "_denoised.pfm"), filtered_framebuffer, image_width, image_height);
			}
			for (AOVKind kind : aovs)
			{
				const std::vector<Color> data = aov_buffers.resolve(kind);
				const std::string base = image_io::replace_extension(output_filename, std::string("_") + aov_name(kind));
				image_io::write_pfm(base + ".pfm", data, image_width, image_height);
				image_io::write_ppm_bytes(base + ".ppm", AOVBuffers::visualize(kind, data, max_depth), image_width, image_height);
			}
		}

		if (verbose)
			std::clog << "\rDone.                 \n";
	}

	std::vector<RenderTile> build_tiles() const
	{
		// Split the whole image into small independent blocks for parallel scheduling.
		std::vector<RenderTile> tiles;
		tiles.reserve(((image_width + kTileSize - 1) / kTileSize) * ((image_height + kTileSize - 1) / kTileSize));

		for (int y = 0; y < image_height; y += kTileSize)
		{
			for (int x = 0; x < image_width; x += kTileSize)
			{
				tiles.push_back(RenderTile{
					x,
					std::min(x + kTileSize, image_width),
					y,
					std::min(y + kTileSize, image_height)
				});
			}
		}

		return tiles;
	}

	bool can_open_output_file(const std::string& filename) const
	{
		std::ofstream out(filename);
		if (!out.is_open())
		{
			std::cerr << "Error: Cannot open file: " << filename << "\n";
			return false;
		}

		return true;
	}

	void resolve_guides()
	{
		for (size_t i = 0; i < guide_buffer.size(); ++i)
			guide_buffer[i] = aov_buffers.guide(i);
	}

	int     image_height;       // Rendered image height
	int		sqrt_spp;           // Number of strata per axis (sqrt_spp^2 <= spp)
	double  recip_sqrt_spp;     // 1 / sqrt_spp
	Point3  camera_center;      // Camera center
	Point3  pixel00_center;     // Location of pixel 0, 0
	Vector3 pixel_delta_u;      // Offset to pixel to the right
	Vector3 pixel_delta_v;      // Offset to pixel below
	Vector3 u, v, w;

	Vector3   defocus_disk_u;   // Defocus disk horizontal radius
	Vector3   defocus_disk_v;   // Defocus disk vertical radius

	std::shared_ptr<Environment> environment = nullptr;
	LightSampler light_sampler;

	void initialize()
	{
		image_height = static_cast<int>(image_width / aspect_ratio);
		// Ensure the height always greater than 1
		image_height = (image_height < 1) ? 1 : image_height;

		// The first sqrt_spp^2 samples of a pixel are stratified over a sqrt_spp x sqrt_spp grid;
		// any remaining samples (spp not a perfect square) are uniformly jittered.
		sqrt_spp = std::max(1, static_cast<int>(std::sqrt(sample_per_pixel)));
		recip_sqrt_spp = 1.0 / sqrt_spp;

		camera_center = lookfrom;

		// Camera
		double theta = degrees_to_radians(vfov);
		double h = std::tan(theta / 2);
		auto viewport_height = 2 * h * focus_dist;
		auto viewport_width = viewport_height * (double(image_width) / image_height);

		w = normalize(lookfrom - lookat);
		u = normalize(cross(up, w));
		v = cross(w, u);

		// Calculate the edge of viewport
		auto viewport_u = viewport_width * u;
		auto viewport_v = viewport_height * -v;

		// Calculate the distance form the current pixel to the next pixel
		pixel_delta_u = viewport_u / image_width;
		pixel_delta_v = viewport_v / image_height;

		// Calculate the location of the upper left pixel
		auto viewport_upper_left = camera_center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;		
		pixel00_center = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

		// Calculate the radius of the defocus disk, which is determined by the focus distance and the defocus angle
		auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
		defocus_disk_u = u * defocus_radius;
		defocus_disk_v = v * defocus_radius;
	}

	// Sub-pixel offset in [-0.5, 0.5]^2 for one sample. The first sqrt_spp^2 samples visit the
	// strata in a per-pixel pseudo-random order (fixes the progressive "top rows first" bias).
	Vector3 sample_pixel_offset(int i, int j, int sample_index) const
	{
		const int strata = sqrt_spp * sqrt_spp;
		if (sample_index < strata)
		{
			const uint32_t pixel_seed = static_cast<uint32_t>(
				rng::splitmix64(seed ^ (static_cast<uint64_t>(j) * image_width + i)));
			const uint32_t cell = rng::permute(static_cast<uint32_t>(sample_index), static_cast<uint32_t>(strata), pixel_seed);
			const int s_i = static_cast<int>(cell) % sqrt_spp;
			const int s_j = static_cast<int>(cell) / sqrt_spp;
			return Vector3(
				((s_i + random_double()) * recip_sqrt_spp) - 0.5,
				((s_j + random_double()) * recip_sqrt_spp) - 0.5,
				0);
		}
		return Vector3(random_double() - 0.5, random_double() - 0.5, 0);
	}

	Ray get_ray(int i, int j, int sample_index) const
	{
		auto offset = sample_pixel_offset(i, j, sample_index);
		auto pixel_sample = pixel00_center +
							((i + offset.x()) * pixel_delta_u) +
							((j + offset.y()) * pixel_delta_v);

		auto ray_origin = (defocus_angle <= 0) ? camera_center : defocus_disk_sample();
		auto ray_direction = pixel_sample - ray_origin;
		auto ray_time = random_double();

		return Ray(ray_origin, ray_direction, ray_time);
	}

	Point3 defocus_disk_sample() const
	{
		// Returns a random point in the camera defocus disk.
		auto p = random_in_unit_disk();
		return camera_center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
	}

};
