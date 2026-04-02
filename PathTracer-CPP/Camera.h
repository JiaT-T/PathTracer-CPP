#pragma once
#include <fstream>
#include "Hittable.h"
#include "My_Common.h"
#include "Material.h"

class Camera
{
public : 
	// Image
	double aspect_ratio     = 16.0 / 9.0;
	int    image_width      = 400;
	int    sample_per_pixel = 100;
	int    max_depth        = 10;

	double  vfov     = 90;
	Vector3 lookfrom = Point3(0, 0, 0);
	Vector3 lookat   = Point3(0, 0, -1);
	Vector3 up       = Vector3(0, 1, 0);

	double defocus_angle = 0;  // Variation angle of rays through each pixel
	double focus_dist = 10;    // Distance from camera lookfrom point to plane of perfect focus

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
	Vector3 u, v, w;

	Vector3   defocus_disk_u;   // Defocus disk horizontal radius
	Vector3   defocus_disk_v;   // Defocus disk vertical radius

	void initialize()
	{
		image_height = static_cast<int>(image_width / aspect_ratio);
		// Ensure the height always greater than 1
		image_height = (image_height < 1) ? 1 : image_height;

		camera_center = lookfrom;

		// Camera
		double theta = degrees_to_radians(vfov);
		double h = std::tan(theta / 2);
		auto viewport_height = 2 * h * focus_dist;
		auto viewport_width = viewport_height * (double(image_width) / image_height);
		pixel_sample_scale = 1.0 / sample_per_pixel;

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

	// According to the offset from sample_square(), 
	// Casting rays randomly around the location: (i, j)
	Ray get_ray(int i, int j) const
	{
		auto offset = sample_square();
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

	Color ray_color(const Ray& ray, const int depth, const Hittable& world)
	{
		// If reach the max depth, return black
		// And end the recursion
		if (depth <= 0)
			return Color(0, 0, 0);

		HitRecord rec;
		Interval ray_t(0.001, infinity);
		if (world.Hit(ray, ray_t, rec))
		{
			Ray scattered;
			Color attenuation;
			if (rec.mat->Scatter(ray, rec, attenuation, scattered))
				return attenuation * ray_color(scattered, depth - 1, world);
			return Color(0, 0, 0);
		}
		Vector3 uint_direction = normalize(ray.direction());
		// [-1, 1] -> [0, 1]
		auto a = 0.5 * (uint_direction.y() + 1.0);
		return (1.0 - a) * Color(1.0, 1.0, 1.0) + a * Color(0.5, 0.7, 1.0);
	}
};

