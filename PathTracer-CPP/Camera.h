#pragma once
#include <fstream>
#include "Hittable.h"
#include "My_Common.h"

class Camera
{
public : 
	// Image
	double aspect_ratio     = 16.0 / 9.0;
	int    image_width      = 400;
	int    sample_per_pixel = 100;
	int    max_depth        = 10;

	void Render(const Hittable& world)
	{
		initialize();

		// Create an output file stream
		std::ofstream out("image.ppm");
		if (!out.is_open())
		{
			std::cerr << "Error: Cannot open file.\n";
			return;
		}

		// Renderer
		out << "P3\n" << image_width << ' ' << image_height << "\n255\n";

		for (int j = 0; j < image_height; j++)
		{
			std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
			for (int i = 0; i < image_width; i++)
			{
				Color pixel_color(0, 0, 0);
				for (int sample = 0; sample < sample_per_pixel; sample++)
				{
					Ray r = get_ray(i, j);
					pixel_color += ray_color(r, max_depth, world);
				}
				write_color(out, pixel_color * pixel_sample_scale);
			}
		}
		std::clog << "\rDone.                 \n";
	}

private :
	int     image_height;       // Rendered image height
	double  pixel_sample_scale; // Color scale factor for a sum of pixel samples
	Point3  camera_center;      // Camera center
	Point3  pixel00_center;     // Location of pixel 0, 0
	Vector3 pixel_delta_u;      // Offset to pixel to the right
	Vector3 pixel_delta_v;      // Offset to pixel below

	void initialize()
	{
		image_height = static_cast<int>(image_width / aspect_ratio);
		// Ensure the height always greater than 1
		image_height = (image_height < 1) ? 1 : image_height;

		camera_center = Point3(0, 0, 0);

		// Camera
		auto focal_length = 1.0;
		auto viewport_height = 2.0;
		auto viewport_width = viewport_height * (double(image_width) / image_height);
		pixel_sample_scale = 1.0 / sample_per_pixel;

		// Calculate the edge of viewport
		auto viewport_u = Vector3(viewport_width, 0, 0);
		auto viewport_v = Vector3(0, -viewport_height, 0);

		// Calculate the distance form the current pixel to the next pixel
		pixel_delta_u = viewport_u / image_width;
		pixel_delta_v = viewport_v / image_height;

		// Calculate the location of the upper left pixel
		auto viewport_upper_left = camera_center - Vector3(0, 0, focal_length) - viewport_u / 2 - viewport_v / 2;
		pixel00_center = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);
	}

	Vector3 sample_square() const
	{
		// Sampling randomly around [-.5,-.5] to [+.5,+.5] in a squared pixel
		return Vector3(random_double() - 0.5, random_double() - 0.5, 0);
	}

	// According to the offset from sample_square(), 
	// Casting rays randomly around the location: (i, j)
	Ray get_ray(int i, int j) const
	{
		auto offset = sample_square();
		auto pixel_sample = pixel00_center +
							((i + offset.x()) * pixel_delta_u) +
							((j + offset.y()) * pixel_delta_v);

		auto ray_origin = camera_center;
		auto ray_direction = pixel_sample - ray_origin;

		return Ray(ray_origin, ray_direction);
	}

	Color ray_color(const Ray& ray, const int depth, const Hittable& world)
	{
		// If reach the max depth, return black
		// And end the recursion
		if (depth <= 0)
			return Color(0, 0, 0);

		HitRecord rec;
		if (world.Hit(ray, 0.001, infinity, rec))
		{
			//Vector3 direction = random_on_hemisphere(rec.n);
			Vector3 direction = rec.n + random_unit_vector();
			return 0.5 * ray_color(Ray(rec.p, direction), depth - 1, world);
		}
		Vector3 uint_direction = normalize(ray.direction());
		// [-1, 1] -> [0, 1]
		auto a = 0.5 * (uint_direction.y() + 1.0);
		return (1.0 - a) * Color(1.0, 1.0, 1.0) + a * Color(0.5, 0.7, 1.0);
	}
};

