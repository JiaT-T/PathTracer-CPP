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
#include "Constant_Medium.h"

void Bouncing_Spheres();
void Checker_Spheres();
void Earth();
void Perlin_Spheres();
void Quads();
void Lights_Test();
void Cornell_Box();
void Cornell_Smoke();
void Chapter_Two_Final_Scene(int image_width, int sample_per_pixel, int max_depth);

int main()
{
	switch (7)
	{
		case 1:  Bouncing_Spheres();					  break;
		case 2:  Checker_Spheres();						  break;
		case 3:  Earth();								  break;
		case 4:  Perlin_Spheres();						  break;
		case 5:  Quads();								  break;	
		case 6:  Lights_Test();						  	  break;
		case 7:  Cornell_Box();							  break;
		case 8:  Cornell_Smoke();						  break;
		case 9:  Chapter_Two_Final_Scene(800, 10000, 40); break;
		default: Chapter_Two_Final_Scene(400,   250,  4); break;
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

	cam.background = Color(0.70, 0.80, 1.00);

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

	cam.background = Color(0.70, 0.80, 1.00);

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

	cam.background = Color(0.70, 0.80, 1.00);

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

	cam.background = Color(0.70, 0.80, 1.00);

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

	cam.background = Color(0.70, 0.80, 1.00);

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world);
	}
}

void Lights_Test()
{
	Hittable_List world;

	auto noiseTex = make_shared<Noise_Texture>(4);
	world.add(make_shared<Sphere>(Point3(0, -1000, 0), 1000, make_shared<Lambertian>(noiseTex)));
	world.add(make_shared<Sphere>(Point3(0, 2, 0), 2, make_shared<Lambertian>(noiseTex)));

	auto light = make_shared<Diffuse_Light>(Color(4, 4, 4));
	world.add(make_shared<Quad>(Point3(3, 1, -2), Vector3(2, 0, 0), Vector3(0, 2, 0), light));
	auto red_light = make_shared<Diffuse_Light>(Color(4, 0.2, 0.2));
	world.add(make_shared<Sphere>(Point3(0, 7.2, 0), 2, red_light));

	Camera cam;

	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;
	cam.background = Color(0, 0, 0);

	cam.vfov = 20;
	cam.lookfrom = Point3(26, 3, 6);
	cam.lookat = Point3(0, 2, 0);
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

void Cornell_Box()
{
	Hittable_List world;

	// Materials
	auto   red = make_shared<Lambertian>(Color(.65, .05, .05));
	auto white = make_shared<Lambertian>(Color(.73, .73, .73));
	auto green = make_shared<Lambertian>(Color(.12, .45, .15));
	auto light = make_shared<Diffuse_Light>(Color(15, 15, 15));

	// Objects
	world.add(make_shared<Quad>(Point3(555,   0,   0),   Vector3(   0, 555, 0),  Vector3(0,   0,  555), green));
	world.add(make_shared<Quad>(Point3(  0,   0,   0),   Vector3(   0, 555, 0),  Vector3(0,   0,  555),   red));
	world.add(make_shared<Quad>(Point3(343, 554, 332),   Vector3(-130,   0, 0),  Vector3(0,   0, -105), light));
	world.add(make_shared<Quad>(Point3(  0,   0,   0),   Vector3( 555,   0, 0),  Vector3(0,   0,  555), white));
	world.add(make_shared<Quad>(Point3(555, 555, 555),   Vector3(-555,   0, 0),  Vector3(0,   0, -555), white));
	world.add(make_shared<Quad>(Point3(  0,   0, 555),   Vector3( 555,   0, 0),  Vector3(0, 555,    0), white));


	std::shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), white);
	//shared_ptr<Material> aluminum = make_shared<Metal>(Color(0.8, 0.85, 0.88), 0.0);
	//shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), aluminum);
	box1 = make_shared<Rotate_Y>(box1, 15);
	box1 = make_shared<Translation>(box1, Vector3(265, 0, 295));
	world.add(box1);

	//std::shared_ptr<Hittable> box2 = Box(Point3(0, 0, 0), Point3(165, 165, 165), white);
	//box2 = make_shared<Rotate_Y>(box2, -18);
	//box2 = make_shared<Translation>(box2, Vector3(130, 0, 65));
	//world.add(box2);

	auto glass = std::make_shared<Dielectric>(1.5);
	world.add(make_shared<Sphere>(Point3(190, 90, 190), 90, glass));

	// Lights
	auto empty_material = std::shared_ptr<Material>();
	Hittable_List lights;
	lights.add(make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), empty_material));
	lights.add(make_shared<Sphere>(Point3(190, 90, 190), 90, empty_material));

	// BVH
	world = Hittable_List(std::make_shared<BVH_Node>(world));

	// Camera
	Camera cam;

	cam.aspect_ratio = 1.0;
	cam.image_width = 600;
	cam.sample_per_pixel = 1500;
	cam.max_depth = 50;
	cam.background = Color(0, 0, 0);

	cam.vfov = 40;
	cam.lookfrom = Point3(278, 278, -800);
	cam.lookat = Point3(278, 278, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	std::clog << "Start rendering...\n";
	{
		// Timing
		Timer timer;
		// Rendering
		cam.Render(world, lights);
	}
}

void Cornell_Smoke()
{
	Hittable_List world;

	// Materials
	auto   red = make_shared<Lambertian>(Color(.65, .05, .05));
	auto white = make_shared<Lambertian>(Color(.73, .73, .73));
	auto green = make_shared<Lambertian>(Color(.12, .45, .15));
	auto light = make_shared<Diffuse_Light>(Color(7, 7, 7));

	// Objects
	world.add(make_shared<Quad>(Point3(555, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), green));
	world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), red));
	world.add(make_shared<Quad>(Point3(113, 554, 127), Vector3(330, 0, 0), Vector3(0, 0, 305), light));
	world.add(make_shared<Quad>(Point3(0, 555, 0), Vector3(555, 0, 0), Vector3(0, 0, 555), white));
	world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(555, 0, 0), Vector3(0, 0, 555), white));
	world.add(make_shared<Quad>(Point3(0, 0, 555), Vector3(555, 0, 0), Vector3(0, 555, 0), white));

	std::shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), white);
	box1 = make_shared<Rotate_Y>(box1, 15);
	box1 = make_shared<Translation>(box1, Vector3(265, 0, 295));
	world.add(std::make_shared<Constant_Medium>(box1, 0.01, Color(0, 0, 0)));

	std::shared_ptr<Hittable> box2 = Box(Point3(0, 0, 0), Point3(165, 165, 165), white);
	box2 = make_shared<Rotate_Y>(box2, -18);
	box2 = make_shared<Translation>(box2, Vector3(130, 0, 65));
	world.add(std::make_shared<Constant_Medium>(box2, 0.01, Color(1, 1, 1)));

	// Camera
	Camera cam;

	cam.aspect_ratio = 1.0;
	cam.image_width = 600;
	cam.sample_per_pixel = 200;
	cam.max_depth = 50;
	cam.background = Color(0, 0, 0);

	cam.vfov = 40;
	cam.lookfrom = Point3(278, 278, -800);
	cam.lookat = Point3(278, 278, 0);
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

void Chapter_Two_Final_Scene(int image_width, int sample_per_pixel, int max_depth)
{
	// A branch of BVH
	Hittable_List boxes1;

	auto ground = make_shared<Lambertian>(Color(0.15, 0.83, 0.53));

	int box_count = 20;
	for (int z = 0; z < box_count; z++)
	{
		for (int x = 0; x < box_count; x++)
		{
			auto w = 100.0;

			auto x0 = -1000 + x * w;
			auto z0 = -1000 + z * w;
			auto y0 = 0.0;

			auto x1 = x0 + w;
			auto z1 = z0 + w;
			auto y1 = random_double(1.0, 101.0);

			boxes1.add(Box(Point3(x0, y0, z0), Point3(x1, y1, z1), ground));
		}
	}

	Hittable_List world;
	world.add(std::make_shared<BVH_Node>(boxes1));

	auto light = make_shared<Diffuse_Light>(Color(7, 7, 7));
	world.add(make_shared<Quad>(Point3(123, 554, 147), Vector3(300, 0, 0), Vector3(0, 0, 265), light));

	auto center1 = Point3(400, 400, 200);
	auto center2 = center1 + Vector3(30, 0, 0);
	auto sphere_material = make_shared<Lambertian>(Color(0.7, 0.3, 0.1));
	world.add(make_shared<Sphere>(center1, center2, 50, sphere_material));

	world.add(make_shared<Sphere>(Point3(260, 150, 45), 50, make_shared<Dielectric>(1.5)));
	world.add(make_shared<Sphere>(
		Point3(0, 150, 145), 50, make_shared<Metal>(Color(0.8, 0.8, 0.9), 1.0)
	));

	auto boundary = make_shared<Sphere>(Point3(360, 150, 145), 70, make_shared<Dielectric>(1.5));
	world.add(boundary);
	world.add(make_shared<Constant_Medium>(boundary, 0.2, Color(0.2, 0.4, 0.9)));
	boundary = make_shared<Sphere>(Point3(0, 0, 0), 5000, make_shared<Dielectric>(1.5));
	world.add(make_shared<Constant_Medium>(boundary, .0001, Color(1, 1, 1)));

	auto emat = make_shared<Lambertian>(make_shared<Image_Texture>("earthmap.jpg"));
	world.add(make_shared<Sphere>(Point3(400, 200, 400), 100, emat));
	auto pertext = make_shared<Noise_Texture>(0.2);
	world.add(make_shared<Sphere>(Point3(220, 280, 300), 80, make_shared<Lambertian>(pertext)));

	Hittable_List boxes2;
	auto white = make_shared<Lambertian>(Color(.73, .73, .73));
	int ns = 1000;
	for (int j = 0; j < ns; j++) {
		boxes2.add(make_shared<Sphere>(Point3::random(0, 165), 10, white));
	}

	world.add(make_shared<Translation>(
				make_shared<Rotate_Y>(
					make_shared<BVH_Node>(boxes2), 15), Vector3(-100, 270, 395)));

	Camera cam;

	cam.aspect_ratio = 1.0;
	cam.image_width = image_width;
	cam.sample_per_pixel = sample_per_pixel;
	cam.max_depth = max_depth;
	cam.background = Color(0, 0, 0);

	cam.vfov = 40;
	cam.lookfrom = Point3(478, 278, -600);
	cam.lookat = Point3(278, 278, 0);
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