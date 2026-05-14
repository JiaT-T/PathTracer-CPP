#pragma once
#include <algorithm>
#include <chrono>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <execution>

#include "Hittable.h"
#include "My_Common.h"
#include "Material.h"
#include "PDF.h"
#include "PPMPreviewWindow.h"
#include "Environment.h"

static double power_heuristic(double pdf_a, double pdf_b)
{
	double a2 = pdf_a * pdf_a;
	double b2 = pdf_b * pdf_b;
	double denom = a2 + b2;
	return a2 <= 0.0 ? 0.0 : a2 / denom;
}

static double power_heuristic(double pdf_a, int sample_count_a, double pdf_b, int sample_count_b)
{
	const double weighted_a = sample_count_a * pdf_a;
	const double weighted_b = sample_count_b * pdf_b;
	return power_heuristic(weighted_a, weighted_b);
}

class Camera
{
public : 
	enum class Render_Mode
	{
		Serial,
		Parallel
	};

	// Image
	double aspect_ratio     = 16.0 / 9.0;
	int    image_width      = 400;
	int    sample_per_pixel = 100;
	int    max_depth        = 10;
	Color  background;
	std::string output_filename = "image.ppm";
	Render_Mode render_mode = Render_Mode::Parallel;

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

	// Ver.1
	void Render(const Hittable& world, PPMPreviewWindow* preview = nullptr)
	{
		render_impl(
			[&](int i, int j)
			{
				Color pixel_color(0, 0, 0);
				for (int s_j = 0; s_j < sqrt_spp; s_j++)
				{
					for (int s_i = 0; s_i < sqrt_spp; s_i++)
					{
						Ray r = get_ray(i, j, s_i, s_j);
						pixel_color += ray_color(r, max_depth, world);
					}
				}
				return pixel_color * pixel_sample_scale;
			},
			preview);
	}

	// Ver.2
	void Render(const Hittable& world, const Hittable& lights, PPMPreviewWindow* preview = nullptr)
	{
		render_impl(
			[&](int i, int j)
			{
				Color pixel_color(0, 0, 0);
				for (int s_j = 0; s_j < sqrt_spp; s_j++)
				{
					for (int s_i = 0; s_i < sqrt_spp; s_i++)
					{
						Ray r = get_ray(i, j, s_i, s_j);
						pixel_color += ray_color(r, max_depth, world, lights);
					}
				}
				return pixel_color * pixel_sample_scale;
			},
			preview);
	}

private :
	struct RenderTile
	{
		int x_begin = 0;
		int x_end = 0;
		int y_begin = 0;
		int y_end = 0;
	};

	static constexpr int kTileSize = 4;

	template <typename PixelShader>
	void render_impl(PixelShader&& pixel_shader, PPMPreviewWindow* preview)
	{
		initialize();

		// Create an output file stream
		std::ofstream out(output_filename);
		if (!out.is_open())
		{
			std::cerr << "Error: Cannot open file.\n";
			return;
		}

		// Renderer
		out << "P3\n" << image_width << ' ' << image_height << "\n255\n";
		std::vector<Color> framebuffer(static_cast<size_t>(image_width) * image_height);
		std::vector<unsigned char> preview_pixels;
		auto preview_start_time = std::chrono::steady_clock::now();
		auto last_preview_update_time = preview_start_time;
		std::mutex progress_mutex;
		std::atomic<int> completed_rows{ 0 };
		std::atomic<int> completed_tiles{ 0 };
		std::vector<std::atomic<int>> row_progress(static_cast<size_t>(image_height));
		for (auto& row_counter : row_progress)
			row_counter.store(0, std::memory_order_relaxed);
		const std::vector<RenderTile> tiles = build_tiles();
		const int total_tiles = static_cast<int>(tiles.size());
		if (preview)
		{
			preview_pixels.assign(static_cast<size_t>(image_width) * image_height * 4, 0);
			preview->UpdateImage(preview_pixels, 0, 0.0);
		}

		auto render_tile = [&](const RenderTile& tile)
		{
			std::vector<Color_Bytes> tile_preview_bytes;
			if (preview)
			{
				tile_preview_bytes.resize(static_cast<size_t>(tile_width(tile) * tile_height(tile)));
			}

			for (int j = tile.y_begin; j < tile.y_end; j++)
			{
				for (int i = tile.x_begin; i < tile.x_end; i++)
				{
					Color final_color = pixel_shader(i, j);
					size_t framebuffer_index = static_cast<size_t>(j) * image_width + i;
					framebuffer[framebuffer_index] = final_color;

					if (preview)
					{
						size_t local_x = static_cast<size_t>(i - tile.x_begin);
						size_t local_y = static_cast<size_t>(j - tile.y_begin);
						size_t local_index = local_y * tile_width(tile) + local_x;
						tile_preview_bytes[local_index] = to_color_bytes(final_color);
					}
				}
			}

			auto now = std::chrono::steady_clock::now();
			const int tile_completed_rows = update_completed_rows(tile, row_progress, completed_rows);
			const int finished_tiles = completed_tiles.fetch_add(1, std::memory_order_relaxed) + 1;
			const bool is_last_tile = finished_tiles == total_tiles;

			std::lock_guard<std::mutex> lock(progress_mutex);
			if (preview)
			{
				blit_tile_preview(tile, tile_preview_bytes, preview_pixels);
			}

			if (is_last_tile ||
				std::chrono::duration<double>(now - last_preview_update_time).count() >= 0.033)
			{
				std::clog << "\rScanlines remaining: " << (image_height - tile_completed_rows) << ' ' << std::flush;

				if (preview && !preview->IsClosed())
				{
					std::chrono::duration<double> elapsed_seconds = now - preview_start_time;
					preview->UpdateImage(preview_pixels, tile_completed_rows, elapsed_seconds.count());
				}

				last_preview_update_time = now;
			}
		};

		if (render_mode == Render_Mode::Parallel)
			std::for_each(std::execution::par, tiles.begin(), tiles.end(), render_tile);
		else
			std::for_each(tiles.begin(), tiles.end(), render_tile);

		for (int j = 0; j < image_height; j++)
		{
			for (int i = 0; i < image_width; i++)
			{
				write_color(out, framebuffer[static_cast<size_t>(j) * image_width + i]);
			}
		}

		if (preview && !preview->IsClosed())
		{
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration<double>(now - last_preview_update_time).count() > 0.0)
			{
				std::chrono::duration<double> elapsed_seconds = now - preview_start_time;
				preview->UpdateImage(preview_pixels, image_height, elapsed_seconds.count());
			}
		}
		std::clog << "\rDone.                 \n";
	}

	std::vector<RenderTile> build_tiles() const
	{
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

	static int tile_width(const RenderTile& tile)
	{
		return tile.x_end - tile.x_begin;
	}

	static int tile_height(const RenderTile& tile)
	{
		return tile.y_end - tile.y_begin;
	}

	void blit_tile_preview(
		const RenderTile& tile,
		const std::vector<Color_Bytes>& tile_preview_bytes,
		std::vector<unsigned char>& preview_pixels)
	{
		const size_t local_width = static_cast<size_t>(tile_width(tile));
		for (int j = tile.y_begin; j < tile.y_end; j++)
		{
			for (int i = tile.x_begin; i < tile.x_end; i++)
			{
				size_t local_x = static_cast<size_t>(i - tile.x_begin);
				size_t local_y = static_cast<size_t>(j - tile.y_begin);
				size_t local_index = local_y * local_width + local_x;
				const Color_Bytes& bytes = tile_preview_bytes[local_index];
				size_t pixel_index = (static_cast<size_t>(j) * image_width + static_cast<size_t>(i)) * 4;
				preview_pixels[pixel_index + 0] = bytes.b;
				preview_pixels[pixel_index + 1] = bytes.g;
				preview_pixels[pixel_index + 2] = bytes.r;
				preview_pixels[pixel_index + 3] = 255;
			}
		}
	}

	int update_completed_rows(
		const RenderTile& tile,
		std::vector<std::atomic<int>>& row_progress,
		std::atomic<int>& completed_rows)
	{
		const int width = tile_width(tile);
		for (int j = tile.y_begin; j < tile.y_end; j++)
		{
			const int finished_pixels = row_progress[static_cast<size_t>(j)].fetch_add(width, std::memory_order_relaxed) + width;
			if (finished_pixels == image_width)
			{
				completed_rows.fetch_add(1, std::memory_order_relaxed);
			}
		}

		return completed_rows.load(std::memory_order_relaxed);
	}

	int     image_height;       // Rendered image height
	double  pixel_sample_scale; // Color scale factor for a sum of pixel samples
	int		sqrt_spp;           // Square root of number of samples per pixel
	double  recip_sqrt_spp;     // 1 / sqrt_spp
	Point3  camera_center;      // Camera center
	Point3  pixel00_center;     // Location of pixel 0, 0
	Vector3 pixel_delta_u;      // Offset to pixel to the right
	Vector3 pixel_delta_v;      // Offset to pixel below
	Vector3 u, v, w;

	Vector3   defocus_disk_u;   // Defocus disk horizontal radius
	Vector3   defocus_disk_v;   // Defocus disk vertical radius

	std::shared_ptr<Environment> environment = nullptr;

	void initialize()
	{
		image_height = static_cast<int>(image_width / aspect_ratio);
		// Ensure the height always greater than 1
		image_height = (image_height < 1) ? 1 : image_height;

		sqrt_spp = static_cast<int>(std::sqrt(sample_per_pixel));
		recip_sqrt_spp = 1.0 / sqrt_spp;
		pixel_sample_scale = 1.0 / (sqrt_spp * sqrt_spp);

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

	Vector3 sample_square() const
	{
		// Sampling randomly around [-.5,-.5] to [+.5,+.5] in a squared pixel
		return Vector3(random_double() - 0.5, random_double() - 0.5, 0);
	}

	Vector3 sample_square_stratified(int s_i, int s_j) const
	{
		// Remap 2D point per pixel to [-0.5, 0.5]
		auto pixel_x = ((s_i + random_double()) * recip_sqrt_spp) - 0.5;
		auto pixel_y = ((s_j + random_double()) * recip_sqrt_spp) - 0.5;

		return Vector3(pixel_x, pixel_y, 0);
	}

	// According to the offset from sample_square(), 
	// Casting rays randomly around the location: (i, j)
	Ray get_ray(int i, int j, int s_i, int s_j) const
	{
		auto offset = sample_square_stratified(s_i, s_j);
		auto pixel_sample = pixel00_center +
							((i + offset.x()) * pixel_delta_u) +
							((j + offset.y()) * pixel_delta_v);

		auto ray_origin = (defocus_angle < 0) ? camera_center : defocus_disk_sample();
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

	// Ver.1
	Color ray_color(const Ray& ray, const int depth, const Hittable& world)
	{
		return ray_color(ray, depth, world, true);
	}

	// Ver.2
	Color ray_color(const Ray& ray, const int depth, const Hittable& world, const Hittable& lights)
	{
		return ray_color(ray, depth, world, lights, true);
	}

	int first_bounce_samples = 4;

	Color miss_radiance(const Ray& ray) const
	{
		if (environment)
			return environment->radiance(ray.direction());
		return background;
	}

	// Build a PDF for sampling light sources automatically,
	// which can be a mixture of geometry-based sampling and environment-based sampling
	std::shared_ptr<PDF> build_light_pdf(const Hittable& lights, const Point3& origin) const
	{
		auto p_geo = std::make_shared<Hittable_PDF>(lights, origin);

		if (!environment)
			return p_geo;

		auto p_env = std::make_shared<Environment_PDF>(*environment);

		const double env_power = environment->sampling_power_estimate();
		const double geo_power = lights.sampling_power_estimate();

		const double sum = env_power + geo_power;
		double weight_geo = (sum > 0.0) ? (geo_power / sum) : 0.5;
		weight_geo = std::clamp(weight_geo, 0.05, 0.95);

		return std::make_shared<Mixture_PDF>(p_geo, p_env, weight_geo);
	}

	// Suppress immediate emission / environment when the caller already handles direct lighting explicitly.
	Color ray_color(const Ray& ray, const int depth, const Hittable& world, bool allow_emission)
	{
		if (depth <= 0)
			return Color(0, 0, 0);

		HitRecord rec;
		if (!world.Hit(ray, Interval(0.001, infinity), rec))
			return allow_emission ? miss_radiance(ray) : Color(0, 0, 0);

		Scattered_Record s_rec{};
		const Color emitted_color =
			allow_emission ? rec.mat->emitted(ray, rec, rec.u, rec.v, rec.p)
			: Color(0, 0, 0);

		if (!rec.mat->Scatter(ray, rec, s_rec))
			return emitted_color;

		if (s_rec.skip_pdf)
		{
			return emitted_color + s_rec.attenuation * ray_color(s_rec.skip_pdf_ray, depth - 1, world, true);
		}

		Ray scattered(rec.p, s_rec.p_pdf->generate(), ray.time());
		const double pdf_value = s_rec.p_pdf->value(scattered.direction());
		if (pdf_value <= 0.0)
			return emitted_color;

		const Color f = rec.mat->Eval(ray, rec, scattered);
		const double cos_theta = std::max(dot(rec.n, normalize(scattered.direction())), 0.0);
		const Color sample_color = ray_color(scattered, depth - 1, world, true);
		const Color scattered_color = (s_rec.attenuation * f * sample_color * cos_theta) / pdf_value;

		return emitted_color + scattered_color;
	}

	// Suppress immediate emission / environment when the caller already handles direct lighting explicitly.
	Color ray_color(const Ray& ray, const int depth, const Hittable& world, const Hittable& lights, bool allow_emission)
	{
		if (depth <= 0)
			return Color(0, 0, 0);

		HitRecord rec;
		if (!world.Hit(ray, Interval(0.001, infinity), rec))
			return allow_emission ? miss_radiance(ray) : Color(0, 0, 0);

		Scattered_Record s_rec{};
		const Color emitted_color =
			allow_emission ? rec.mat->emitted(ray, rec, rec.u, rec.v, rec.p)
			: Color(0, 0, 0);

		if (!rec.mat->Scatter(ray, rec, s_rec))
			return emitted_color;

		if (s_rec.skip_pdf)
		{
			// Delta paths cannot use explicit direct-light sampling, so keep emission enabled.
			return emitted_color + s_rec.attenuation * ray_color(s_rec.skip_pdf_ray, depth - 1, world, lights, true);
		}

		auto p_light = build_light_pdf(lights, rec.p);

		const int bounce = max_depth - depth;
		const int light_direct_sample_count = (bounce == 0) ? first_bounce_samples : 1;
		const int bsdf_direct_sample_count = 1;

		// Spend extra first-bounce budget on explicit light samples instead of duplicating full recursion.
		Color light_direct_sum(0, 0, 0);
		for (int i = 0; i < light_direct_sample_count; ++i)
		{
			light_direct_sum += sample_direct_light_once(
				ray,
				rec,
				s_rec,
				world,
				p_light,
				light_direct_sample_count,
				bsdf_direct_sample_count);
		}

		// Keep one BSDF-side direct sample so sharp glossy lobes still have a matching strategy.
		Color bsdf_direct_sum(0, 0, 0);
		for (int i = 0; i < bsdf_direct_sample_count; ++i)
		{
			bsdf_direct_sum += sample_direct_bsdf_once(
				ray,
				rec,
				s_rec,
				world,
				p_light,
				light_direct_sample_count,
				bsdf_direct_sample_count);
		}

		const Color direct_lighting =
			light_direct_sum / static_cast<double>(light_direct_sample_count) +
			bsdf_direct_sum / static_cast<double>(bsdf_direct_sample_count);
		const Color indirect_lighting =
			sample_indirect_once(ray, depth, world, lights, rec, s_rec);

		return emitted_color + direct_lighting + indirect_lighting;
	}

	// Evaluate one explicit light-side direct sample without spawning a recursive subtree.
	Color sample_direct_light_once(
		const Ray& ray,
		const HitRecord& rec,
		const Scattered_Record& s_rec,
		const Hittable& world,
		const std::shared_ptr<PDF>& p_light,
		const int light_sample_count,
		const int bsdf_sample_count)
	{
		Vector3 dir = p_light->generate();
		Ray scattered(rec.p, dir, ray.time());

		const double light_pdf = p_light->value(dir);
		if (light_pdf <= 0.0)
			return Color(0, 0, 0);

		const double bsdf_pdf = s_rec.p_pdf->value(scattered.direction());
		const double mis_weight =
			power_heuristic(light_pdf, light_sample_count, bsdf_pdf, bsdf_sample_count);

		const Color f = rec.mat->Eval(ray, rec, scattered);
		const double cos_theta = std::max(dot(rec.n, normalize(scattered.direction())), 0.0);
		if (cos_theta <= 0.0)
			return Color(0, 0, 0);

		const Color direct_radiance = trace_direct_radiance(scattered, world);

		return (s_rec.attenuation * f * direct_radiance * cos_theta * mis_weight)
			/ light_pdf;
	}

	// Evaluate one explicit BSDF-side direct sample so glossy reflections still have a matching estimator.
	Color sample_direct_bsdf_once(
		const Ray& ray,
		const HitRecord& rec,
		const Scattered_Record& s_rec,
		const Hittable& world,
		const std::shared_ptr<PDF>& p_light,
		const int light_sample_count,
		const int bsdf_sample_count)
	{
		Vector3 dir = s_rec.p_pdf->generate();
		Ray scattered(rec.p, dir, ray.time());

		const double bsdf_pdf = s_rec.p_pdf->value(dir);
		if (bsdf_pdf <= 0.0)
			return Color(0, 0, 0);

		const double light_pdf = p_light->value(scattered.direction());
		const double mis_weight =
			power_heuristic(bsdf_pdf, bsdf_sample_count, light_pdf, light_sample_count);

		const Color f = rec.mat->Eval(ray, rec, scattered);
		const double cos_theta = std::max(dot(rec.n, normalize(scattered.direction())), 0.0);
		if (cos_theta <= 0.0)
			return Color(0, 0, 0);

		const Color direct_radiance = trace_direct_radiance(scattered, world);

		return (s_rec.attenuation * f * direct_radiance * cos_theta * mis_weight)
			/ bsdf_pdf;
	}

	// Continue the path only once through the BSDF after the explicit direct-light estimate.
	Color sample_indirect_once(
		const Ray& ray,
		const int depth,
		const Hittable& world,
		const Hittable& lights,
		const HitRecord& rec,
		const Scattered_Record& s_rec)
	{
		Vector3 dir = s_rec.p_pdf->generate();
		Ray scattered(rec.p, dir, ray.time());

		const double pdf_value = s_rec.p_pdf->value(dir);
		if (pdf_value <= 0.0)
			return Color(0, 0, 0);

		const Color f = rec.mat->Eval(ray, rec, scattered);
		const double cos_theta = std::max(dot(rec.n, normalize(scattered.direction())), 0.0);
		if (cos_theta <= 0.0)
			return Color(0, 0, 0);

		double p_survival = 1.0;
		const int bounce = max_depth - depth;
		if (bounce >= 3)
		{
			const Color rr_weight = (s_rec.attenuation * f * cos_theta) / pdf_value;

			p_survival = std::clamp(
				std::fmax(rr_weight.x(), std::fmax(rr_weight.y(), rr_weight.z())),
				0.0001,
				0.9999);

			if (random_double() > p_survival)
				return Color(0, 0, 0);
		}

		// The next call should not add immediate emission again because direct lighting
		// has already been estimated explicitly at this bounce.
		const Color indirect_radiance = ray_color(scattered, depth - 1, world, lights, false);

		return (s_rec.attenuation * f * indirect_radiance * cos_theta)
			/ (pdf_value * p_survival);
	}

	// Trace a single visibility ray to either an emissive surface or the environment.
	Color trace_direct_radiance(const Ray& shadow_ray, const Hittable& world) const
	{
		HitRecord light_rec;
		if (!world.Hit(shadow_ray, Interval(0.001, infinity), light_rec))
			return miss_radiance(shadow_ray);

		return light_rec.mat->emitted(
			shadow_ray,
			light_rec,
			light_rec.u,
			light_rec.v,
			light_rec.p);
	}
};
