
#include "My_Common.h"
#include "Hittable.h"
#include "Hittable_List.h"
#include "Sphere.h"
#include "Camera.h"
#include "Material.h"

int main()
{
	// Create World
	Hittable_List world;

	//auto material_ground = make_shared<Lambertian>(Color(0.8, 0.8, 0.0));
	//auto material_center = make_shared<Lambertian>(Color(0.1, 0.2, 0.5));
	//auto material_left = make_shared<Dielectric>(1.50);
	//auto material_left_inner = make_shared<Dielectric>(1.00 / 1.50);
	//auto material_right = make_shared<Metal>(Color(0.8, 0.6, 0.2), 0.8);	

	//world.add(make_shared<Sphere>(Point3( 0.0, -100.5, -1.0), 100.0, material_ground));
	//world.add(make_shared<Sphere>(Point3( 0.0,    0.0, -1.0),   0.5, material_center));
	//world.add(make_shared<Sphere>(Point3(-1.0,    0.0, -1.0),   0.5, material_left));
	//world.add(make_shared<Sphere>(Point3(-1.0,    0.0, -1.0),   0.4, material_left_inner));
	//world.add(make_shared<Sphere>(Point3( 1.0,    0.0, -1.0),   0.5, material_right));


	auto ground_material = make_shared<Lambertian>(Color(0.5, 0.5, 0.5));
	world.add(make_shared<Sphere>(Point3(0, -1000, 0), 1000, ground_material));

	for (int a = -12; a < 12; a++)
	{
		for (int b = -12; b < 12; b++)
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

	// Rendering
	cam.Render(world);
}