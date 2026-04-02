#include "My_Common.h"
#include "Hittable.h"
#include "Hittable_List.h"
#include "Sphere.h"
#include "Camera.h"
#include "Material.h"
#include "BVH.h"
#include "Timer.h"
#include "Texture.h"
#include "Quad.h"

void Bouncing_Spheres();
void Checker_Spheres();
void Earth();
void Perlin_Spheres();
void Quads();

int main()
{
	switch (4)
	{
	case 1:
		Bouncing_Spheres(); break;
	case 2:
		Checker_Spheres();  break;
	case 3:
		Earth(); break;
	case 4:
		Perlin_Spheres(); break;
	case 5:
		Quads(); break;	
	}
}

void Bouncing_Spheres()
{
	// Create World
	Hittable_List world;

	auto checker = make_shared<Checker_Texture>(0.2, Color(.2, .3, .1), Color(.9, .9, .9));
	world.add(make_shared<Sphere>(Point3(0, -1000, 0), 1000, std::make_shared<Lambertian>(checker)));

	for (int a = -5; a < 5; a++)
	{
		for (int b = -5; b < 5; b++)
		{
			auto choose_mat = random_double();
			Point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

			if ((center - Point3(4, 0.2, 0)).length() > 0.9)
			{
				std::shared_ptr<Material> sphere_material;

				if (choose_mat < 0.8)
				{
					// Diffuse
					auto albedo = random() * random();
					sphere_material = make_shared<Lambertian>(albedo);
					Point3 center2 = center + Vector3(0, random_double(), 0);
					world.add(make_shared<Sphere>(center, center2, 0.2, sphere_material));
				}
				else if (choose_mat < 0.95)
				{
					// Metal
					auto albedo = random(0.5, 1);
					auto fuzz = random_double(0, 0.5);
					sphere_material = make_shared<Metal>(albedo, fuzz);
					world.add(make_shared<Sphere>(center, 0.2, sphere_material));
				}
				else
				{
					// Glass
					sphere_material = make_shared<Dielectric>(1.5);
					world.add(make_shared<Sphere>(center, 0.2, sphere_material));
				}
			}
		}
	}

	auto material1 = make_shared<Dielectric>(1.5);
	world.add(make_shared<Sphere>(Point3(0, 1, 0), 1.0, material1));

	auto material2 = make_shared<Lambertian>(Color(0.4, 0.2, 0.1));
	world.add(make_shared<Sphere>(Point3(-4, 1, 0), 1.0, material2));

	auto material3 = make_shared<Metal>(Color(0.7, 0.6, 0.5), 0.0);
	world.add(make_shared<Sphere>(Point3(4, 1, 0), 1.0, material3));

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	// Create Camera
	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;

	cam.vfov = 20;
	cam.lookfrom = Vector3(13, 2, 3);
	cam.lookat = Vector3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0.6;
	cam.focus_dist = 10;

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world);
	}
}

void Checker_Spheres()
{
	Hittable_List world;
	auto checker = std::make_shared<Checker_Texture>(0.32, Color(.2, .3, .1), Color(.9, .9, .9));

	world.add(std::make_shared<Sphere>(Point3(0, 10, 0), 10, std::make_shared<Lambertian>(checker)));
	world.add(std::make_shared<Sphere>(Point3(0, -10, 0), 10, std::make_shared<Lambertian>(checker)));

	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;

	cam.vfov = 20;
	cam.lookfrom = Point3(13, 2, 3);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world);
	}
}

void Earth()
{
	auto earth_texture = std::make_shared<Image_Texture>("earthmap.jpg");
	auto earth_surface = std::make_shared<Lambertian>(earth_texture);
	auto globe = std::make_shared<Sphere>(Point3(0, 0, 0), 2, earth_surface);

	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;

	cam.vfov = 20;
	cam.lookfrom = Point3(0, 0, 12);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	cam.Render(Hittable_List(globe));
}

void Perlin_Spheres()
{
	Hittable_List world;

	auto perlin_texture = std::make_shared<Noise_Texture>(4);
	world.add(make_shared<Sphere>(Point3(0, -1000, 0), 1000, make_shared<Lambertian>(perlin_texture)));
	world.add(make_shared<Sphere>(Point3(0, 2, 0), 2, make_shared<Lambertian>(perlin_texture)));

	Camera cam;

	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 200;
	cam.max_depth = 50;

	cam.vfov = 20;
	cam.lookfrom = Point3(13, 2, 3);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world);
	}
}

void Quads()
{
	Hittable_List world;

	auto left_red = make_shared<Lambertian>(Color(1.0, 0.2, 0.2));
	auto back_green = make_shared<Lambertian>(Color(0.2, 1.0, 0.2));
	auto right_blue = make_shared<Lambertian>(Color(0.2, 0.2, 1.0));
	auto upper_orange = make_shared<Lambertian>(Color(1.0, 0.5, 0.0));
	auto lower_teal = make_shared<Lambertian>(Color(0.2, 0.8, 0.8));

	world.add(make_shared<Quad>(Point3(-3, -2, 5), Vector3(0, 0, -4), Vector3(0, 4, 0), left_red));
	world.add(make_shared<Quad>(Point3(-2, -2, 0), Vector3(4, 0, 0), Vector3(0, 4, 0), back_green));
	world.add(make_shared<Quad>(Point3(3, -2, 1), Vector3(0, 0, 4), Vector3(0, 4, 0), right_blue));
	world.add(make_shared<Quad>(Point3(-2, 3, 1), Vector3(4, 0, 0), Vector3(0, 0, 4), upper_orange));
	world.add(make_shared<Quad>(Point3(-2, -3, 5), Vector3(4, 0, 0), Vector3(0, 0, -4), lower_teal));

	Camera cam;

	cam.aspect_ratio = 1.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;

	cam.vfov = 80;
	cam.lookfrom = Point3(0, 0, 9);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world);
	}
}