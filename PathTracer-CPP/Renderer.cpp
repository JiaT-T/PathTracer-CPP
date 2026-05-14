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
#include "PPMPreviewWindow.h"

#include "Triangle.h"
#include "ObjLoader.h"

void Bouncing_Spheres();
void Checker_Spheres();
void Earth();
void Perlin_Spheres();
void Quads();
void Lights_Test();
void Cornell_Box();
void Cornell_Smoke();
void Chapter_Two_Final_Scene(int image_width, int sample_per_pixel, int max_depth);
void Triangle_Test();
void ObjTest();
void Teapot();
void Sponza();
void PBR_Test();
void Obj_PBR_Test();
void PBR_Benchmark();
void PBR_Normal_Map_Test();
void PBR_IBL_Test();
void README_Showcase();

void RenderAndPreview(Camera& cam, const Hittable& world)
{
	PPMPreviewWindow preview(cam.output_filename, cam.image_width, cam.output_height());
	Timer timer;
	cam.Render(world, &preview);
	const double render_seconds = timer.stop();
	preview.SetFinished(render_seconds);
	preview.WaitUntilClosed();
}
void RenderAndPreview(Camera& cam, const Hittable& world, const Hittable& lights)
{
	PPMPreviewWindow preview(cam.output_filename, cam.image_width, cam.output_height());
	Timer timer;
	cam.Render(world, lights, &preview);
	const double render_seconds = timer.stop();
	preview.SetFinished(render_seconds);
	preview.WaitUntilClosed();
}

int main()
{
	switch (19)
	{
		case  1:  Bouncing_Spheres();					    break;
		case  2:  Checker_Spheres();					    break;
		case  3:  Earth();								    break;
		case  4:  Perlin_Spheres();						    break;
		case  5:  Quads();								    break;	
		case  6:  Lights_Test();						    break;
		case  7:  Cornell_Box();						    break;
		case  8:  Cornell_Smoke();						    break;
		case  9:  Chapter_Two_Final_Scene(800, 512, 40);    break;
		case 10:  Triangle_Test();                          break;
		case 11:  ObjTest();								break;
		case 12:  Teapot();									break;
		case 13:  Sponza();                                 break;
		case 14:  PBR_Test();                               break;
		case 15:  PBR_Benchmark();                          break;
		case 16:  PBR_Normal_Map_Test();                    break;
		case 17:  Obj_PBR_Test();                           break;
		case 18:  PBR_IBL_Test();                           break;
		case 19:  README_Showcase();                        break;
	}
}

std::pair<Hittable_List, Hittable_List> BuildPBRValidationScene()
{
	Hittable_List world;
	Hittable_List lights;

	auto light_mat = std::make_shared<Diffuse_Light>(Color(18, 18, 18));
	auto ground_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

	world.add(std::make_shared<Quad>(
		Point3(-8.0, -1.0, -8.0),
		Vector3(16.0, 0.0, 0.0),
		Vector3(0.0, 0.0, 16.0),
		ground_mat));

	auto make_pbr = [](const Color& base_color, double roughness, double metallic)
	{
		return std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(base_color),
			nullptr,
			std::make_shared<Solid_Color>(roughness, roughness, roughness),
			std::make_shared<Solid_Color>(metallic, metallic, metallic));
	};

	world.add(std::make_shared<Sphere>(
		Point3(-3.0, 0.5, 0.0),
		0.5,
		make_pbr(Color(0.95, 0.65, 0.2), 0.08, 0.0)));
	world.add(std::make_shared<Sphere>(
		Point3(-1.0, 0.5, 0.0),
		0.5,
		make_pbr(Color(0.95, 0.65, 0.2), 0.75, 0.0)));
	world.add(std::make_shared<Sphere>(
		Point3(1.0, 0.5, 0.0),
		0.5,
		make_pbr(Color(0.95, 0.65, 0.2), 0.08, 1.0)));
	world.add(std::make_shared<Sphere>(
		Point3(3.0, 0.5, 0.0),
		0.5,
		make_pbr(Color(0.95, 0.65, 0.2), 0.75, 1.0)));

	auto quad_light = std::make_shared<Quad>(
		Point3(-2.0, 5.5, 2.0),
		Vector3(4.0, 0.0, 0.0),
		Vector3(0.0, 0.0, -4.0),
		light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	return {
		Hittable_List(std::make_shared<BVH_Node>(world)),
		Hittable_List(lights)
	};
}

Camera MakePBRValidationCamera()
{
	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 800;
	cam.sample_per_pixel = 400;
	cam.max_depth = 20;

	cam.vfov = 35;
	cam.lookfrom = Point3(0, 2.2, 8.5);
	cam.lookat = Point3(0, 0.7, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.02, 0.02, 0.02);
	return cam;
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
	RenderAndPreview(cam, world);
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
	RenderAndPreview(cam, world);
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

	std::clog << "Start rendering...\n";
	RenderAndPreview(cam, Hittable_List(globe));
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
	RenderAndPreview(cam, world);
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
	RenderAndPreview(cam, world);
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
	RenderAndPreview(cam, world);
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
	cam.sample_per_pixel = 500;
	cam.max_depth = 50;
	cam.background = Color(0, 0, 0);

	cam.vfov = 40;
	cam.lookfrom = Point3(278, 278, -800);
	cam.lookat = Point3(278, 278, 0);
	cam.up = Vector3(0, 1, 0);

	cam.defocus_angle = 0;

	std::clog << "Start rendering...\n";
	RenderAndPreview(cam, world, lights);
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
	RenderAndPreview(cam, world);
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
	RenderAndPreview(cam, world);
}

void Triangle_Test()
{
	Hittable_List world;
	Hittable_List lights;

	auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
	auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

	auto gray_triangle = std::make_shared<Triangle>(Point3(-2.0, -2.0, -1.0),Point3(2.0, -2.0, -1.0),Point3(0.0, 2.0, -1.0),gray_mat);	
	auto light_triangle = std::make_shared<Triangle>(Point3(-2.0, 5.0, -2.0), Point3(2.0, 5.0, -2.0), Point3(0.0, 5.0, 2.0), light_mat);
	auto ground = std::make_shared<Quad>(Point3(-5, -2.01, -5),Vector3(10, 0, 0),Vector3(0, 0, 10),gray_mat);

	world.add(ground);
	world.add(gray_triangle);
	world.add(light_triangle);
	lights.add(light_triangle);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 50;
	cam.max_depth = 10;

	cam.vfov = 80;
	cam.lookfrom = Point3(0, 5, 9);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.70, 0.80, 1.00);

	std::clog << "Start rendering Triangle PDF Test...\n";
	RenderAndPreview(cam, world, lights);
}

void ObjTest()
{
	Hittable_List world;
	Hittable_List lights;

	// Material
	auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
	auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));


	// Obj
	std::clog << "Loading OBJ model...\n";
	auto model_mesh = ObjLoader::load("Model/dragon.obj", gray_mat);

	if (model_mesh)
	{
		auto bvh_model = std::make_shared<BVH_Node>(*model_mesh);

		world.add(std::make_shared<Translation>(bvh_model, Vector3(0, -5, 0)));
		std::clog << "Model loaded and BVH built successfully.\n";
	}
	else
	{
		std::clog << "Cannot find model!\n";
	}

	// Light
	auto quad_light = std::make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 50;
	cam.max_depth = 10;

	cam.vfov = 80;
	cam.lookfrom = Point3(0, 5, 9);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.70, 0.80, 1.00);

	std::clog << "Start rendering Triangle PDF Test...\n";
	RenderAndPreview(cam, world, lights);
}

void Teapot()
{
	Hittable_List world;
	Hittable_List lights;

	auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
	auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

	std::clog << "Loading OBJ model...\n";
	auto model_mesh = ObjLoader::load("Model/teapot.obj", gray_mat);

	if (model_mesh)
	{
		auto bvh_model = std::make_shared<BVH_Node>(*model_mesh);
		world.add(bvh_model);
		std::clog << "Model loaded and BVH built successfully.\n";
	}
	else
	{
		std::clog << "Cannot find model!\n";
	}

	auto quad_light = std::make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 50;
	cam.max_depth = 10;

	cam.vfov = 45;
	cam.lookfrom = Point3(0, 5, 9);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.70, 0.80, 1.00);

	std::clog << "Start rendering Triangle PDF Test...\n";
	RenderAndPreview(cam, world, lights);
}

void Sponza()
{
	Hittable_List world;
	Hittable_List lights;

	auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
	auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

	std::clog << "Loading OBJ model...\n";
	auto model_mesh = ObjLoader::load("Model/sponza.obj", gray_mat);

	if (model_mesh)
	{
		auto bvh_model = std::make_shared<BVH_Node>(*model_mesh);
		world.add(bvh_model);
		std::clog << "Model loaded and BVH built successfully.\n";
	}
	else
	{
		std::clog << "Cannot find model!\n";
	}

	auto quad_light = std::make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 50;
	cam.max_depth = 10;

	cam.vfov = 45;
	cam.lookfrom = Point3(0, 5, 9);
	cam.lookat = Point3(0, 0, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.70, 0.80, 1.00);

	std::clog << "Start rendering Sponza ...\n";
	RenderAndPreview(cam, world, lights);
}

void PBR_Test()
{
	Hittable_List world;
	Hittable_List lights;

	auto red = std::make_shared<Lambertian>(Color(.65, .05, .05));
	auto white = std::make_shared<Lambertian>(Color(.73, .73, .73));
	auto green = std::make_shared<Lambertian>(Color(.12, .45, .15));
	auto light_mat = std::make_shared<Diffuse_Light>(Color(18, 18, 18));

	world.add(make_shared<Quad>(Point3(555, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), green));
	world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), red));
	world.add(make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat));
	world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(555, 0, 0), Vector3(0, 0, 555), white));
	world.add(make_shared<Quad>(Point3(555, 555, 555), Vector3(-555, 0, 0), Vector3(0, 0, -555), white));
	world.add(make_shared<Quad>(Point3(0, 0, 555), Vector3(555, 0, 0), Vector3(0, 555, 0), white));

	auto ornament_pbr = std::make_shared<PBR_Material>(
		std::make_shared<Image_Texture>("images/ChristmasTreeOrnament019/ChristmasTreeOrnament019_1K-JPG_Color.jpg", color_space::SRGB),
		std::make_shared<Image_Texture>("images/ChristmasTreeOrnament019/ChristmasTreeOrnament019_1K-JPG_NormalGL.jpg", color_space::Linear),
		std::make_shared<Image_Texture>("images/ChristmasTreeOrnament019/ChristmasTreeOrnament019_1K-JPG_Roughness.jpg", color_space::Linear),
		std::make_shared<Image_Texture>("images/ChristmasTreeOrnament019/ChristmasTreeOrnament019_1K-JPG_Metalness.jpg", color_space::Linear));

	std::clog << "Loading sphere OBJ for PBR test...\n";
	auto sphere_mesh = ObjLoader::load("Model/sphere.obj", ornament_pbr, true);
	if (sphere_mesh)
	{
		std::shared_ptr<Hittable> cube = std::make_shared<BVH_Node>(*sphere_mesh);
		cube = std::make_shared<Scale>(cube, 100.0);
		cube = std::make_shared<Translation>(cube, Vector3(278.0, 160.0, 278.0));
		world.add(cube);
	}
	else
	{
		std::clog << "Failed to load sphere.obj\n";
	}

	auto quad_light = std::make_shared<Quad>(
		Point3(343, 554, 332),
		Vector3(-130, 0, 0),
		Vector3(0, 0, -105),
		light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 600;
	cam.sample_per_pixel = 500;
	cam.max_depth = 50;
	cam.vfov = 40;
	cam.lookfrom = Point3(278, 278, -800);
	cam.lookat = Point3(278, 200, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0, 0, 0);
	cam.output_filename = "pbr_sphere_ornament_test.ppm";

	std::clog << "Start rendering Cornell-box sphere ornament PBR test scene...\n";
	RenderAndPreview(cam, world, lights);
}

void PBR_Benchmark()
{
	auto [world, lights] = BuildPBRValidationScene();

	Camera serial_cam = MakePBRValidationCamera();
	serial_cam.render_mode = Camera::Render_Mode::Serial;
	serial_cam.output_filename = "benchmark_serial.ppm";

	Camera parallel_cam = MakePBRValidationCamera();
	parallel_cam.render_mode = Camera::Render_Mode::Parallel;
	parallel_cam.output_filename = "benchmark_parallel.ppm";

	std::clog << "Benchmark scene: PBR validation\n";
	std::clog << "Resolution: " << serial_cam.image_width << "x" << serial_cam.output_height()
			  << ", spp: " << serial_cam.sample_per_pixel
			  << ", depth: " << serial_cam.max_depth << "\n";

	std::clog << "\n[Benchmark] Serial render...\n";
	Timer serial_timer;
	serial_cam.Render(world, lights);
	const double serial_seconds = serial_timer.stop();

	std::clog << "\n[Benchmark] Parallel render...\n";
	Timer parallel_timer;
	parallel_cam.Render(world, lights);
	const double parallel_seconds = parallel_timer.stop();

	const double speedup = parallel_seconds > 0.0 ? serial_seconds / parallel_seconds : 0.0;
	const double reduction = serial_seconds > 0.0
		? (1.0 - parallel_seconds / serial_seconds) * 100.0
		: 0.0;

	std::clog << "\n[Benchmark] Serial:   " << serial_seconds << " s\n";
	std::clog << "[Benchmark] Parallel: " << parallel_seconds << " s\n";
	std::clog << "[Benchmark] Speedup:  " << speedup << "x\n";
	std::clog << "[Benchmark] Time reduced: " << reduction << "%\n";
}

void PBR_Normal_Map_Test()
{
	Hittable_List world;
	Hittable_List lights;

	auto light_mat = std::make_shared<Diffuse_Light>(Color(120, 120, 120));
	auto fill_light_mat = std::make_shared<Diffuse_Light>(Color(18, 18, 18));
	auto ground_mat = std::make_shared<Lambertian>(Color(0.65, 0.65, 0.65));

	auto base_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Color.jpg", color_space::SRGB);
	auto roughness_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Roughness.jpg", color_space::Linear);
	auto metallic_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Metalness.jpg", color_space::Linear);
	auto normal_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_NormalGL.jpg", color_space::Linear);

	auto pbr_with_normal = std::make_shared<PBR_Material>(base_tex, normal_tex, roughness_tex, metallic_tex);
	auto pbr_without_normal = std::make_shared<PBR_Material>(base_tex, nullptr, roughness_tex, metallic_tex);

	world.add(std::make_shared<Quad>(
		Point3(-8.0, -1.0, -8.0),
		Vector3(16.0, 0.0, 0.0),
		Vector3(0.0, 0.0, 16.0),
		ground_mat));

	auto add_panel = [&](const Point3& origin, std::shared_ptr<Material> mat)
	{
		const Point3 p0 = origin;
		const Point3 p1 = origin + Vector3(2.4, 0.0, 0.0);
		const Point3 p2 = origin + Vector3(2.4, 2.4, 0.0);
		const Point3 p3 = origin + Vector3(0.0, 2.4, 0.0);

		world.add(std::make_shared<Triangle>(
			p0, p1, p2,
			TexCoord2(0.0, 0.0), TexCoord2(1.0, 0.0), TexCoord2(1.0, 1.0),
			mat));
		world.add(std::make_shared<Triangle>(
			p0, p2, p3,
			TexCoord2(0.0, 0.0), TexCoord2(1.0, 1.0), TexCoord2(0.0, 1.0),
			mat));
	};

	add_panel(Point3(-3.2, -0.2, 0.0), pbr_with_normal);
	add_panel(Point3(0.8, -0.2, 0.0), pbr_without_normal);

	world.add(std::make_shared<Sphere>(
		Point3(-4.8, 0.2, 1.4),
		0.8,
		std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(Color(0.95, 0.65, 0.2)),
			nullptr,
			std::make_shared<Solid_Color>(0.08, 0.08, 0.08),
			std::make_shared<Solid_Color>(1.0, 1.0, 1.0))));

	world.add(std::make_shared<Sphere>(
		Point3(4.2, 0.2, 1.4),
		0.8,
		std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(Color(0.95, 0.65, 0.2)),
			nullptr,
			std::make_shared<Solid_Color>(0.75, 0.75, 0.75),
			std::make_shared<Solid_Color>(0.0, 0.0, 0.0))));

	auto quad_light = std::make_shared<Quad>(
		Point3(-2.8, 4.8, 3.2),
		Vector3(5.6, 0.0, 0.0),
		Vector3(0.0, 0.0, -6.2),
		light_mat);
	world.add(quad_light);
	lights.add(quad_light);

	auto fill_light = std::make_shared<Quad>(
		Point3(-5.4, 0.8, 3.6),
		Vector3(0.0, 3.0, 0.0),
		Vector3(0.0, 0.0, -4.8),
		fill_light_mat);
	world.add(fill_light);
	lights.add(fill_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 960;
	cam.sample_per_pixel = 400;
	cam.max_depth = 20;
	cam.vfov = 27;
	cam.lookfrom = Point3(0.0, 1.45, 6.6);
	cam.lookat = Point3(0.0, 1.0, 0.35);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.16, 0.16, 0.16);
	cam.output_filename = "pbr_normal_map_test.ppm";

	std::clog << "Start rendering PBR normal map validation scene...\n";
	RenderAndPreview(cam, world, lights);
}

void Obj_PBR_Test()
{
	Hittable_List world;
	Hittable_List lights;

	auto red = make_shared<Lambertian>(Color(.65, .05, .05));
	auto white = make_shared<Lambertian>(Color(.73, .73, .73));
	auto green = make_shared<Lambertian>(Color(.12, .45, .15));
	auto light = make_shared<Diffuse_Light>(Color(50, 50, 50));
	auto fallback_mat = make_shared<Lambertian>(Color(.73, .73, .73));

	world.add(make_shared<Quad>(Point3(555,   0,   0), Vector3(   0, 555,   0), Vector3(0,   0,  555), green));
	world.add(make_shared<Quad>(Point3(  0,   0,   0), Vector3(   0, 555,   0), Vector3(0,   0,  555), red));
	world.add(make_shared<Quad>(Point3(343, 554, 332), Vector3(-130,   0,   0), Vector3(0,   0, -105), light));
	world.add(make_shared<Quad>(Point3(  0,   0,   0), Vector3( 555,   0,   0), Vector3(0,   0,  555), white));
	world.add(make_shared<Quad>(Point3(555, 555, 555), Vector3(-555,   0,   0), Vector3(0,   0, -555), white));
	world.add(make_shared<Quad>(Point3(  0,   0, 555), Vector3( 555,   0,   0), Vector3(0, 555,    0), white));

	std::clog << "Loading Obj_PBRTest sphere for automatic material test...\n";
	auto sphere_mesh = ObjLoader::load("Model/Obj_PBRTest/Sphere.obj", fallback_mat, false);
	if (sphere_mesh)
	{
		std::shared_ptr<Hittable> sphere = std::make_shared<BVH_Node>(*sphere_mesh);
		sphere = std::make_shared<Scale>(sphere, 90.0);
		sphere = std::make_shared<Translation>(sphere, Vector3(278.0, 90.0, 278.0));
		world.add(sphere);
	}
	else
	{
		std::clog << "Failed to load Model/Obj_PBRTest/Sphere.obj\n";
	}

	lights.add(make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), std::shared_ptr<Material>()));

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 1.0;
	cam.image_width = 600;
	cam.sample_per_pixel = 500;
	cam.max_depth = 50;
	cam.background = Color(0, 0, 0);

	cam.vfov = 40;
	cam.lookfrom = Point3(278, 278, -800);
	cam.lookat = Point3(278, 278, 0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.output_filename = "obj_pbr_test.ppm";

	std::clog << "Start rendering automatic OBJ PBR test scene...\n";
	RenderAndPreview(cam, world, lights);
}

void PBR_IBL_Test()
{
	Hittable_List world;
	Hittable_List lights;

	auto ground_mat = std::make_shared<Lambertian>(Color(0.55, 0.55, 0.55));
	world.add(std::make_shared<Quad>(
		Point3(-12.0, -1.0, -12.0),
		Vector3(24.0, 0.0, 0.0),
		Vector3(0.0, 0.0, 24.0),
		ground_mat));

	auto ornament_pbr = std::make_shared<PBR_Material>(
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Color.jpg",
			color_space::SRGB),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_NormalGL.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Roughness.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Metalness.jpg",
			color_space::Linear));

	world.add(std::make_shared<Sphere>(Point3(0.0, 0.65, 0.0), 1.65, ornament_pbr));

	auto area_light_mat = std::make_shared<Diffuse_Light>(Color(14.0, 14.0, 14.0));
	auto area_light = std::make_shared<Quad>(
		Point3(-8.6, 10.0, 2.4),
		Vector3(3.2, 0.0, 0.0),
		Vector3(0.0, 0.0, 3.2),
		area_light_mat);
	world.add(area_light);
	lights.add(area_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 960;
	cam.sample_per_pixel = 400;
	cam.max_depth = 20;
	cam.vfov = 28;
	cam.lookfrom = Point3(0.0, 1.8, 6.5);
	cam.lookat = Point3(0.0, 0.9, 0.0);
	cam.up = Vector3(0, 1, 0);
	cam.defocus_angle = 0;
	cam.background = Color(0.02, 0.02, 0.02);
	cam.output_filename = "pbr_ibl_test.ppm";
	cam.SetEnvironment(std::make_shared<LatLong_Environment>(
		"images/HDR/suburban_garden_2k.hdr",
		1.5,
		0.0,
		false));

	std::clog << "Start rendering PBR IBL + area light test scene...\n";
	RenderAndPreview(cam, world, lights);
}

void README_Showcase()
{
	Hittable_List world;
	Hittable_List lights;

	auto ground_mat = std::make_shared<Lambertian>(Color(0.56, 0.56, 0.58));
	world.add(std::make_shared<Quad>(
		Point3(-14.0, -1.0, -14.0),
		Vector3(28.0, 0.0, 0.0),
		Vector3(0.0, 0.0, 28.0),
		ground_mat));

	// Reuse the same sphere primitive as the IBL validation scene and lay the
	// spheres out in two readable rows so each feature remains visible in the
	// final README shot.
	auto metal1_pbr = std::make_shared<PBR_Material>(
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Color.jpg",
			color_space::SRGB),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_NormalGL.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Roughness.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal1/Metal049A_2K-JPG_Metalness.jpg",
			color_space::Linear));

	auto gold_pbr = std::make_shared<PBR_Material>(
		std::make_shared<Image_Texture>(
			"images/Metal_Gold/Metal048C_1K-JPG_Color.jpg",
			color_space::SRGB),
		std::make_shared<Image_Texture>(
			"images/Metal_Gold/Metal048C_1K-JPG_NormalGL.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal_Gold/Metal048C_1K-JPG_Roughness.jpg",
			color_space::Linear),
		std::make_shared<Image_Texture>(
			"images/Metal_Gold/Metal048C_1K-JPG_Metalness.jpg",
			color_space::Linear));

	auto earth_mat = std::make_shared<Lambertian>(
		std::make_shared<Image_Texture>("earthmap.jpg", color_space::SRGB));
	auto noise_mat = std::make_shared<Lambertian>(std::make_shared<Noise_Texture>(3.2));
	auto glass_mat = std::make_shared<Dielectric>(1.5);
	auto classic_metal = std::make_shared<Metal>(Color(0.88, 0.90, 0.94), 0.08);

	// Front row: the main PBR, dielectric, and colored-metal reads.
	world.add(std::make_shared<Sphere>(Point3(-3.4, -0.02, 1.0), 0.98, glass_mat));
	world.add(std::make_shared<Sphere>(Point3(0.0, 0.18, 0.8), 1.18, metal1_pbr));
	world.add(std::make_shared<Sphere>(Point3(3.4, -0.02, 1.0), 0.98, gold_pbr));

	// Back row: image texture, classic metal, procedural noise, and volume.
	world.add(std::make_shared<Sphere>(Point3(-5.3, -0.14, -2.0), 0.86, earth_mat));
	world.add(std::make_shared<Sphere>(Point3(-1.8, -0.14, -2.45), 0.82, classic_metal));
	world.add(std::make_shared<Sphere>(Point3(1.8, -0.12, -2.35), 0.86, noise_mat));

	// Keep one participating-medium sphere in the back row so the scene still
	// shows the volume path while remaining visually separated from the glass ball.
	auto fog_boundary = std::make_shared<Sphere>(Point3(5.3, -0.10, -2.1), 0.90, glass_mat);
	world.add(fog_boundary);
	world.add(std::make_shared<Constant_Medium>(fog_boundary, 0.17, Color(0.22, 0.42, 0.88)));

	// Use an explicit area light so the showcase still exercises direct-light MIS
	// instead of relying on HDRI highlights alone.
	auto area_light_mat = std::make_shared<Diffuse_Light>(Color(16.0, 15.0, 14.0));
	auto area_light = std::make_shared<Quad>(
		Point3(-4.0, 6.0, 2.8),
		Vector3(8.0, 0.0, 0.0),
		Vector3(0.0, 0.0, 3.8),
		area_light_mat);
	world.add(area_light);
	lights.add(area_light);

	world = Hittable_List(std::make_shared<BVH_Node>(world));

	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 1280;
	cam.sample_per_pixel = 700;
	cam.max_depth = 25;
	cam.vfov = 27;
	cam.lookfrom = Point3(0.0, 1.55, 12.3);
	cam.lookat = Point3(0.0, 0.35, -0.55);
	cam.up = Vector3(0, 1, 0);
	cam.focus_dist = 12.0;
	cam.defocus_angle = 0.18;
	cam.background = Color(0.02, 0.02, 0.02);
	cam.output_filename = "readme_showcase.ppm";
	cam.SetEnvironment(std::make_shared<LatLong_Environment>(
		"images/HDR/suburban_garden_2k.hdr",
		1.35,
		0.0,
		false));

	std::clog << "Start rendering README showcase scene...\n";
	RenderAndPreview(cam, world, lights);
}
