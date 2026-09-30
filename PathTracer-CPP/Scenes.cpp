#include "Scenes.h"

#include <cctype>

#include "My_Common.h"
#include "Hittable.h"
#include "Hittable_List.h"
#include "Sphere.h"
#include "Material.h"
#include "BVH.h"
#include "Texture.h"
#include "Quad.h"
#include "Constant_Medium.h"
#include "Triangle.h"
#include "ObjLoader.h"
#include "Environment.h"

namespace
{
	SceneDesc make_desc(const std::string& name, Hittable_List world, Camera cam)
	{
		SceneDesc desc;
		desc.name = name;
		desc.world = std::move(world);
		desc.cam = std::move(cam);
		desc.use_lights = false;
		return desc;
	}

	SceneDesc make_desc(const std::string& name, Hittable_List world, Hittable_List lights, Camera cam)
	{
		SceneDesc desc;
		desc.name = name;
		desc.world = std::move(world);
		desc.lights = std::move(lights);
		desc.cam = std::move(cam);
		desc.use_lights = true;
		return desc;
	}

	std::shared_ptr<PBR_Material> make_solid_pbr(const Color& base_color, double roughness, double metallic)
	{
		return std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(base_color),
			nullptr,
			std::make_shared<Solid_Color>(roughness, roughness, roughness),
			std::make_shared<Solid_Color>(metallic, metallic, metallic));
	}

	std::pair<Hittable_List, Hittable_List> BuildPBRValidationScene()
	{
		Hittable_List world;
		Hittable_List lights;

		// Simple matrix scene for checking roughness and metallic response under one area light.
		auto light_mat = std::make_shared<Diffuse_Light>(Color(18, 18, 18));
		auto ground_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

		world.add(std::make_shared<Quad>(
			Point3(-8.0, -1.0, -8.0),
			Vector3(16.0, 0.0, 0.0),
			Vector3(0.0, 0.0, 16.0),
			ground_mat));

		world.add(std::make_shared<Sphere>(Point3(-3.0, 0.5, 0.0), 0.5, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.08, 0.0)));
		world.add(std::make_shared<Sphere>(Point3(-1.0, 0.5, 0.0), 0.5, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.75, 0.0)));
		world.add(std::make_shared<Sphere>(Point3(1.0, 0.5, 0.0), 0.5, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.08, 1.0)));
		world.add(std::make_shared<Sphere>(Point3(3.0, 0.5, 0.0), 0.5, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.75, 1.0)));

		// Diffuse_Light is one-sided (front face = cross(u, v)). u/v are ordered so the light
		// faces down toward the spheres; the previous order emitted upward into the void.
		auto quad_light = std::make_shared<Quad>(
			Point3(-2.0, 5.5, 2.0),
			Vector3(0.0, 0.0, -4.0),
			Vector3(4.0, 0.0, 0.0),
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

	SceneDesc Bouncing_Spheres()
	{
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

		world.add(make_shared<Sphere>(Point3(0, 1, 0), 1.0, make_shared<Dielectric>(1.5)));
		world.add(make_shared<Sphere>(Point3(-4, 1, 0), 1.0, make_shared<Lambertian>(Color(0.4, 0.2, 0.1))));
		world.add(make_shared<Sphere>(Point3(4, 1, 0), 1.0, make_shared<Metal>(Color(0.7, 0.6, 0.5), 0.0)));

		world = Hittable_List(std::make_shared<BVH_Node>(world));

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
		cam.output_filename = "bouncing_spheres.ppm";
		return make_desc("bouncing_spheres", world, cam);
	}

	SceneDesc Checker_Spheres()
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
		cam.output_filename = "checker_spheres.ppm";
		return make_desc("checker_spheres", world, cam);
	}

	SceneDesc Earth()
	{
		auto earth_texture = std::make_shared<Image_Texture>("earthmap.jpg");
		auto earth_surface = std::make_shared<Lambertian>(earth_texture);
		auto globe = std::make_shared<Sphere>(Point3(0, 0, 0), 2, earth_surface);

		Camera cam;
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
		cam.output_filename = "earth.ppm";
		return make_desc("earth", Hittable_List(globe), cam);
	}

	SceneDesc Perlin_Spheres()
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
		cam.output_filename = "perlin_spheres.ppm";
		return make_desc("perlin_spheres", world, cam);
	}

	SceneDesc Quads()
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
		cam.output_filename = "quads.ppm";
		return make_desc("quads", world, cam);
	}

	SceneDesc Lights_Test()
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
		cam.output_filename = "lights_test.ppm";
		return make_desc("lights_test", world, cam);
	}

	SceneDesc Cornell_Box()
	{
		Hittable_List world;

		auto   red = make_shared<Lambertian>(Color(.65, .05, .05));
		auto white = make_shared<Lambertian>(Color(.73, .73, .73));
		auto green = make_shared<Lambertian>(Color(.12, .45, .15));
		auto light = make_shared<Diffuse_Light>(Color(15, 15, 15));

		world.add(make_shared<Quad>(Point3(555,   0,   0),   Vector3(   0, 555, 0),  Vector3(0,   0,  555), green));
		world.add(make_shared<Quad>(Point3(  0,   0,   0),   Vector3(   0, 555, 0),  Vector3(0,   0,  555),   red));
		world.add(make_shared<Quad>(Point3(343, 554, 332),   Vector3(-130,   0, 0),  Vector3(0,   0, -105), light));
		world.add(make_shared<Quad>(Point3(  0,   0,   0),   Vector3( 555,   0, 0),  Vector3(0,   0,  555), white));
		world.add(make_shared<Quad>(Point3(555, 555, 555),   Vector3(-555,   0, 0),  Vector3(0,   0, -555), white));
		world.add(make_shared<Quad>(Point3(  0,   0, 555),   Vector3( 555,   0, 0),  Vector3(0, 555,    0), white));

		std::shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), white);
		box1 = make_shared<Rotate_Y>(box1, 15);
		box1 = make_shared<Translation>(box1, Vector3(265, 0, 295));
		world.add(box1);

		auto glass = std::make_shared<Dielectric>(1.5);
		world.add(make_shared<Sphere>(Point3(190, 90, 190), 90, glass));

		auto empty_material = std::shared_ptr<Material>();
		Hittable_List lights;
		lights.add(make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), empty_material));
		lights.add(make_shared<Sphere>(Point3(190, 90, 190), 90, empty_material));

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
		cam.output_filename = "cornell_box.ppm";
		return make_desc("cornell_box", world, lights, cam);
	}

	SceneDesc Cornell_Smoke()
	{
		Hittable_List world;

		auto   red = make_shared<Lambertian>(Color(.65, .05, .05));
		auto white = make_shared<Lambertian>(Color(.73, .73, .73));
		auto green = make_shared<Lambertian>(Color(.12, .45, .15));
		auto light = make_shared<Diffuse_Light>(Color(7, 7, 7));

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
		cam.output_filename = "cornell_smoke.ppm";
		return make_desc("cornell_smoke", world, cam);
	}

	SceneDesc Chapter_Two_Final_Scene()
	{
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
		world.add(make_shared<Sphere>(Point3(0, 150, 145), 50, make_shared<Metal>(Color(0.8, 0.8, 0.9), 1.0)));

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
		for (int j = 0; j < ns; j++)
			boxes2.add(make_shared<Sphere>(Point3::random(0, 165), 10, white));

		world.add(make_shared<Translation>(
			make_shared<Rotate_Y>(make_shared<BVH_Node>(boxes2), 15), Vector3(-100, 270, 395)));

		Camera cam;
		cam.aspect_ratio = 1.0;
		cam.image_width = 800;
		cam.sample_per_pixel = 512;
		cam.max_depth = 40;
		cam.background = Color(0, 0, 0);

		cam.vfov = 40;
		cam.lookfrom = Point3(478, 278, -600);
		cam.lookat = Point3(278, 278, 0);
		cam.up = Vector3(0, 1, 0);
		cam.defocus_angle = 0;
		cam.output_filename = "chapter_two_final.ppm";
		return make_desc("chapter_two_final", world, cam);
	}

	Camera MakeTriangleTestCamera(double vfov)
	{
		Camera cam;
		cam.aspect_ratio = 1.0;
		cam.image_width = 400;
		cam.sample_per_pixel = 50;
		cam.max_depth = 10;

		cam.vfov = vfov;
		cam.lookfrom = Point3(0, 5, 9);
		cam.lookat = Point3(0, 0, 0);
		cam.up = Vector3(0, 1, 0);
		cam.defocus_angle = 0;
		cam.background = Color(0.70, 0.80, 1.00);
		return cam;
	}

	SceneDesc Triangle_Test()
	{
		Hittable_List world;
		Hittable_List lights;

		auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
		auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

		auto gray_triangle = std::make_shared<Triangle>(Point3(-2.0, -2.0, -1.0), Point3(2.0, -2.0, -1.0), Point3(0.0, 2.0, -1.0), gray_mat);
		auto light_triangle = std::make_shared<Triangle>(Point3(-2.0, 5.0, -2.0), Point3(2.0, 5.0, -2.0), Point3(0.0, 5.0, 2.0), light_mat);
		auto ground = std::make_shared<Quad>(Point3(-5, -2.01, -5), Vector3(10, 0, 0), Vector3(0, 0, 10), gray_mat);

		world.add(ground);
		world.add(gray_triangle);
		world.add(light_triangle);
		lights.add(light_triangle);

		world = Hittable_List(std::make_shared<BVH_Node>(world));

		Camera cam = MakeTriangleTestCamera(80);
		cam.output_filename = "triangle_test.ppm";
		return make_desc("triangle_test", world, lights, cam);
	}

	SceneDesc LoadObjScene(const std::string& name, const std::string& path, double vfov, const Vector3& offset)
	{
		Hittable_List world;
		Hittable_List lights;

		auto light_mat = std::make_shared<Diffuse_Light>(Color(15, 15, 15));
		auto gray_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));

		std::clog << "Loading OBJ model " << path << "...\n";
		auto model_mesh = ObjLoader::load(path, gray_mat);
		if (model_mesh)
		{
			std::shared_ptr<Hittable> bvh_model = std::make_shared<BVH_Node>(*model_mesh);
			if (offset.length_squared() > 0.0)
				bvh_model = std::make_shared<Translation>(bvh_model, offset);
			world.add(bvh_model);
			std::clog << "Model loaded and BVH built successfully.\n";
		}
		else
		{
			std::clog << "Cannot find model " << path << "!\n";
		}

		auto quad_light = std::make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat);
		world.add(quad_light);
		lights.add(quad_light);

		world = Hittable_List(std::make_shared<BVH_Node>(world));

		Camera cam = MakeTriangleTestCamera(vfov);
		cam.output_filename = name + ".ppm";
		return make_desc(name, world, lights, cam);
	}

	SceneDesc PBR_Test()
	{
		Hittable_List world;
		Hittable_List lights;

		// Cornell box keeps lighting controlled while the sphere uses real PBR texture maps.
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

		auto quad_light = std::make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light_mat);
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
		return make_desc("pbr_test", world, lights, cam);
	}

	SceneDesc PBR_Benchmark()
	{
		auto [world, lights] = BuildPBRValidationScene();
		Camera cam = MakePBRValidationCamera();
		cam.output_filename = "benchmark.ppm";
		return make_desc("pbr_benchmark", world, lights, cam);
	}

	SceneDesc PBR_Normal_Map_Test()
	{
		Hittable_List world;
		Hittable_List lights;

		// Side-by-side panels compare the same material with and without the normal map.
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

		world.add(std::make_shared<Sphere>(Point3(-4.8, 0.2, 1.4), 0.8, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.08, 1.0)));
		world.add(std::make_shared<Sphere>(Point3(4.2, 0.2, 1.4), 0.8, make_solid_pbr(Color(0.95, 0.65, 0.2), 0.75, 0.0)));

		// Both lights are one-sided; u/v order makes cross(u, v) face the panels
		// (key light faces -Y, fill light faces +X).
		auto quad_light = std::make_shared<Quad>(
			Point3(-2.8, 4.8, 3.2),
			Vector3(0.0, 0.0, -6.2),
			Vector3(5.6, 0.0, 0.0),
			light_mat);
		world.add(quad_light);
		lights.add(quad_light);

		auto fill_light = std::make_shared<Quad>(
			Point3(-5.4, 0.8, 3.6),
			Vector3(0.0, 0.0, -4.8),
			Vector3(0.0, 3.0, 0.0),
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
		return make_desc("pbr_normal_map_test", world, lights, cam);
	}

	SceneDesc Obj_PBR_Test()
	{
		Hittable_List world;
		Hittable_List lights;

		// This scene checks the automatic OBJ/MTL path for PBR textures.
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
		return make_desc("obj_pbr_test", world, lights, cam);
	}

	std::shared_ptr<PBR_Material> MakeMetal1Material()
	{
		return std::make_shared<PBR_Material>(
			std::make_shared<Image_Texture>("images/Metal1/Metal049A_2K-JPG_Color.jpg", color_space::SRGB),
			std::make_shared<Image_Texture>("images/Metal1/Metal049A_2K-JPG_NormalGL.jpg", color_space::Linear),
			std::make_shared<Image_Texture>("images/Metal1/Metal049A_2K-JPG_Roughness.jpg", color_space::Linear),
			std::make_shared<Image_Texture>("images/Metal1/Metal049A_2K-JPG_Metalness.jpg", color_space::Linear));
	}

	std::shared_ptr<PBR_Material> MakeGoldMaterial()
	{
		return std::make_shared<PBR_Material>(
			std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Color.jpg", color_space::SRGB),
			std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_NormalGL.jpg", color_space::Linear),
			std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Roughness.jpg", color_space::Linear),
			std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Metalness.jpg", color_space::Linear));
	}

	SceneDesc PBR_IBL_Test()
	{
		Hittable_List world;
		Hittable_List lights;

		// HDRI gives the background and environment lighting; the area light adds a controlled highlight.
		auto ground_mat = std::make_shared<Lambertian>(Color(0.55, 0.55, 0.55));
		world.add(std::make_shared<Quad>(
			Point3(-12.0, -1.0, -12.0),
			Vector3(24.0, 0.0, 0.0),
			Vector3(0.0, 0.0, 24.0),
			ground_mat));

		world.add(std::make_shared<Sphere>(Point3(0.0, 0.65, 0.0), 1.65, MakeMetal1Material()));

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
		cam.SetEnvironment(std::make_shared<LatLong_Environment>("images/HDR/suburban_garden_2k.hdr", 1.5, 0.0, false));
		return make_desc("pbr_ibl_test", world, lights, cam);
	}

	SceneDesc README_Showcase()
	{
		Hittable_List world;
		Hittable_List lights;

		auto ground_mat = std::make_shared<Lambertian>(Color(0.56, 0.56, 0.58));
		world.add(std::make_shared<Quad>(
			Point3(-14.0, -1.0, -14.0),
			Vector3(28.0, 0.0, 0.0),
			Vector3(0.0, 0.0, 28.0),
			ground_mat));

		auto metal1_pbr = MakeMetal1Material();
		auto gold_pbr = MakeGoldMaterial();
		auto earth_mat = std::make_shared<Lambertian>(std::make_shared<Image_Texture>("earthmap.jpg", color_space::SRGB));
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
		cam.sample_per_pixel = 1000;
		cam.max_depth = 25;
		cam.vfov = 27;
		cam.lookfrom = Point3(0.0, 1.55, 12.3);
		cam.lookat = Point3(0.0, 0.35, -0.55);
		cam.up = Vector3(0, 1, 0);
		cam.focus_dist = 12.0;
		cam.defocus_angle = 0.18;
		cam.background = Color(0.02, 0.02, 0.02);
		cam.output_filename = "readme_showcase.ppm";
		cam.SetEnvironment(std::make_shared<LatLong_Environment>("images/HDR/suburban_garden_2k.hdr", 1.35, 0.0, false));
		return make_desc("readme_showcase", world, lights, cam);
	}

	// ------------------------------------------------------------------------------------
	// Small, fast scenes for regression tests. All use fixed content and no external meshes.
	// ------------------------------------------------------------------------------------

	Camera MakeTestCamera(int width, double aspect, const Point3& from, const Point3& at, double vfov)
	{
		Camera cam;
		cam.aspect_ratio = aspect;
		cam.image_width = width;
		cam.sample_per_pixel = 64;
		cam.max_depth = 16;
		cam.vfov = vfov;
		cam.lookfrom = from;
		cam.lookat = at;
		cam.up = Vector3(0, 1, 0);
		cam.defocus_angle = 0;
		cam.background = Color(0, 0, 0);
		return cam;
	}

	// White Lambertian sphere under a uniform environment L = 1: every pixel is exactly 1.
	SceneDesc Furnace_Lambert()
	{
		Hittable_List world;
		world.add(std::make_shared<Sphere>(Point3(0, 0, 0), 1.0, std::make_shared<Lambertian>(Color(1, 1, 1))));
		Camera cam = MakeTestCamera(48, 1.0, Point3(0, 0, 4), Point3(0, 0, 0), 30);
		cam.sample_per_pixel = 128;
		cam.SetEnvironment(std::make_shared<Constant_Environment>(Color(1, 1, 1)));
		cam.output_filename = "furnace_lambert.ppm";
		return make_desc("furnace_lambert", world, Hittable_List(), cam);
	}

	// Row of white PBR spheres (top: metallic, bottom: dielectric base = 1), roughness
	// 0.05 / 0.1 / 0.25 / 0.5 / 1.0, uniform environment L = 1. Pixel value = directional
	// albedo of the BSDF at that view angle (convex objects: single bounce).
	SceneDesc Furnace_PBR()
	{
		Hittable_List world;
		const double roughness[5] = { 0.05, 0.1, 0.25, 0.5, 1.0 };
		for (int i = 0; i < 5; ++i)
		{
			const double x = -4.4 + 2.2 * i;
			world.add(std::make_shared<Sphere>(Point3(x, 1.1, 0), 1.0, make_solid_pbr(Color(1, 1, 1), roughness[i], 1.0)));
			world.add(std::make_shared<Sphere>(Point3(x, -1.1, 0), 1.0, make_solid_pbr(Color(1, 1, 1), roughness[i], 0.0)));
		}
		world = Hittable_List(std::make_shared<BVH_Node>(world));
		Camera cam = MakeTestCamera(160, 160.0 / 72.0, Point3(0, 0, 30), Point3(0, 0, 0), 11);
		cam.sample_per_pixel = 128;
		cam.SetEnvironment(std::make_shared<Constant_Environment>(Color(1, 1, 1)));
		cam.output_filename = "furnace_pbr.ppm";
		return make_desc("furnace_pbr", world, Hittable_List(), cam);
	}

	// Non-absorbing isotropic medium (albedo 1) under a uniform environment: every pixel is 1.
	// The boundary sphere is only used to define the medium, it is not part of the world.
	SceneDesc Furnace_Volume()
	{
		Hittable_List world;
		auto boundary = std::make_shared<Sphere>(Point3(0, 0, 0), 1.0, std::make_shared<Lambertian>(Color(1, 1, 1)));
		world.add(std::make_shared<Constant_Medium>(boundary, 1.0, Color(1, 1, 1)));
		Camera cam = MakeTestCamera(48, 1.0, Point3(0, 0, 4), Point3(0, 0, 0), 30);
		cam.sample_per_pixel = 128;
		cam.max_depth = 64;
		cam.SetEnvironment(std::make_shared<Constant_Environment>(Color(1, 1, 1)));
		cam.output_filename = "furnace_volume.ppm";
		return make_desc("furnace_volume", world, Hittable_List(), cam);
	}

	Hittable_List MakeCornellShell(Hittable_List& lights, double light_intensity)
	{
		Hittable_List world;
		auto   red = make_shared<Lambertian>(Color(.65, .05, .05));
		auto white = make_shared<Lambertian>(Color(.73, .73, .73));
		auto green = make_shared<Lambertian>(Color(.12, .45, .15));
		auto light = make_shared<Diffuse_Light>(Color(light_intensity, light_intensity, light_intensity));

		world.add(make_shared<Quad>(Point3(555, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), green));
		world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(0, 555, 0), Vector3(0, 0, 555), red));
		auto light_quad = make_shared<Quad>(Point3(343, 554, 332), Vector3(-130, 0, 0), Vector3(0, 0, -105), light);
		world.add(light_quad);
		lights.add(light_quad);
		world.add(make_shared<Quad>(Point3(0, 0, 0), Vector3(555, 0, 0), Vector3(0, 0, 555), white));
		world.add(make_shared<Quad>(Point3(555, 555, 555), Vector3(-555, 0, 0), Vector3(0, 0, -555), white));
		world.add(make_shared<Quad>(Point3(0, 0, 555), Vector3(555, 0, 0), Vector3(0, 555, 0), white));
		return world;
	}

	// Area light + NEE + MIS, diffuse interreflection, a PBR metal box and a glass sphere.
	SceneDesc Cornell_Small()
	{
		Hittable_List lights;
		Hittable_List world = MakeCornellShell(lights, 15.0);
		auto white = make_shared<Lambertian>(Color(.73, .73, .73));

		std::shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), make_solid_pbr(Color(0.9, 0.9, 0.9), 0.3, 1.0));
		box1 = make_shared<Rotate_Y>(box1, 15);
		box1 = make_shared<Translation>(box1, Vector3(265, 0, 295));
		world.add(box1);

		std::shared_ptr<Hittable> box2 = Box(Point3(0, 0, 0), Point3(165, 165, 165), white);
		box2 = make_shared<Rotate_Y>(box2, -18);
		box2 = make_shared<Translation>(box2, Vector3(130, 0, 65));
		world.add(box2);

		world = Hittable_List(std::make_shared<BVH_Node>(world));
		Camera cam = MakeTestCamera(64, 1.0, Point3(278, 278, -800), Point3(278, 278, 0), 40);
		cam.max_depth = 16;
		cam.output_filename = "cornell_small.ppm";
		return make_desc("cornell_small", world, lights, cam);
	}

	// Homogeneous media inside a Cornell box, lit by the area light (with NEE).
	SceneDesc Volume_Small()
	{
		Hittable_List lights;
		Hittable_List world = MakeCornellShell(lights, 15.0);
		auto white = make_shared<Lambertian>(Color(.73, .73, .73));

		std::shared_ptr<Hittable> box1 = Box(Point3(0, 0, 0), Point3(165, 330, 165), white);
		box1 = make_shared<Rotate_Y>(box1, 15);
		box1 = make_shared<Translation>(box1, Vector3(265, 0, 295));
		world.add(std::make_shared<Constant_Medium>(box1, 0.01, Color(0.2, 0.2, 0.2)));

		std::shared_ptr<Hittable> box2 = Box(Point3(0, 0, 0), Point3(165, 165, 165), white);
		box2 = make_shared<Rotate_Y>(box2, -18);
		box2 = make_shared<Translation>(box2, Vector3(130, 0, 65));
		world.add(std::make_shared<Constant_Medium>(box2, 0.01, Color(0.9, 0.9, 0.9)));

		Camera cam = MakeTestCamera(64, 1.0, Point3(278, 278, -800), Point3(278, 278, 0), 40);
		cam.max_depth = 24;
		cam.output_filename = "volume_small.ppm";
		return make_desc("volume_small", world, lights, cam);
	}

	// Normal-mapped triangle panel + normal-mapped sphere + plain sphere under an area light.
	SceneDesc Normal_Map_Small()
	{
		Hittable_List world;
		Hittable_List lights;

		auto light_mat = std::make_shared<Diffuse_Light>(Color(40, 40, 40));
		auto ground_mat = std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5));
		auto base_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Color.jpg", color_space::SRGB);
		auto normal_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_NormalGL.jpg", color_space::Linear);
		auto rough_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Roughness.jpg", color_space::Linear);
		auto metal_tex = std::make_shared<Image_Texture>("images/Metal_Gold/Metal048C_1K-JPG_Metalness.jpg", color_space::Linear);
		auto mapped = std::make_shared<PBR_Material>(base_tex, normal_tex, rough_tex, metal_tex);
		// Dielectric version so the normal map also drives the diffuse lobe.
		auto mapped_diffuse = std::make_shared<PBR_Material>(
			std::make_shared<Solid_Color>(Color(0.8, 0.8, 0.8)), normal_tex,
			std::make_shared<Solid_Color>(0.6, 0.6, 0.6), std::make_shared<Solid_Color>(0.0, 0.0, 0.0));

		world.add(std::make_shared<Quad>(Point3(-8, -1, -8), Vector3(16, 0, 0), Vector3(0, 0, 16), ground_mat));

		const Point3 o(-3.3, -0.6, 0.0);
		const Point3 p0 = o, p1 = o + Vector3(2.2, 0, 0), p2 = o + Vector3(2.2, 2.2, 0), p3 = o + Vector3(0, 2.2, 0);
		world.add(std::make_shared<Triangle>(p0, p1, p2, TexCoord2(0, 0), TexCoord2(1, 0), TexCoord2(1, 1), mapped_diffuse));
		world.add(std::make_shared<Triangle>(p0, p2, p3, TexCoord2(0, 0), TexCoord2(1, 1), TexCoord2(0, 1), mapped_diffuse));

		world.add(std::make_shared<Sphere>(Point3(0.0, 0.3, 0.3), 1.0, mapped_diffuse));
		world.add(std::make_shared<Sphere>(Point3(2.4, 0.3, 0.3), 1.0, mapped));

		auto quad_light = std::make_shared<Quad>(Point3(-2.5, 4.5, 3.0), Vector3(0, 0, -4.0), Vector3(5.0, 0, 0), light_mat);
		world.add(quad_light);
		lights.add(quad_light);

		world = Hittable_List(std::make_shared<BVH_Node>(world));
		Camera cam = MakeTestCamera(128, 16.0 / 9.0, Point3(0, 1.2, 7.0), Point3(0, 0.4, 0), 40);
		cam.background = Color(0.05, 0.05, 0.05);
		cam.output_filename = "normal_map_small.ppm";
		return make_desc("normal_map_small", world, lights, cam);
	}

	// HDR environment importance sampling + area light, PBR metal and dielectric spheres.
	SceneDesc Environment_Small()
	{
		Hittable_List world;
		Hittable_List lights;

		world.add(std::make_shared<Quad>(Point3(-12, -1, -12), Vector3(24, 0, 0), Vector3(0, 0, 24),
			std::make_shared<Lambertian>(Color(0.55, 0.55, 0.55))));
		world.add(std::make_shared<Sphere>(Point3(-1.3, 0.0, 0.0), 1.0, make_solid_pbr(Color(0.95, 0.93, 0.88), 0.15, 1.0)));
		world.add(std::make_shared<Sphere>(Point3(1.3, 0.0, 0.0), 1.0, make_solid_pbr(Color(0.2, 0.4, 0.8), 0.4, 0.0)));

		auto area_light = std::make_shared<Quad>(Point3(-6.0, 8.0, 2.0), Vector3(3.0, 0, 0), Vector3(0, 0, 3.0),
			std::make_shared<Diffuse_Light>(Color(14, 14, 14)));
		world.add(area_light);
		lights.add(area_light);

		world = Hittable_List(std::make_shared<BVH_Node>(world));
		Camera cam = MakeTestCamera(128, 16.0 / 9.0, Point3(0, 1.5, 7.0), Point3(0, 0.2, 0), 35);
		cam.SetEnvironment(std::make_shared<LatLong_Environment>("images/HDR/suburban_garden_2k.hdr", 1.5, 0.0, false));
		cam.output_filename = "environment_small.ppm";
		return make_desc("environment_small", world, lights, cam);
	}

	std::string to_lower(std::string s)
	{
		for (char& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}
}

const std::vector<SceneEntry>& scene_registry()
{
	static const std::vector<SceneEntry> registry = {
		{ 1, "bouncing_spheres", "RTIOW final scene (defocus, motion blur)", Bouncing_Spheres },
		{ 2, "checker_spheres", "Checker texture", Checker_Spheres },
		{ 3, "earth", "Image texture sphere", Earth },
		{ 4, "perlin_spheres", "Perlin noise texture", Perlin_Spheres },
		{ 5, "quads", "Quad primitives", Quads },
		{ 6, "lights_test", "Emissive quad + sphere, no NEE", Lights_Test },
		{ 7, "cornell_box", "Cornell box with glass sphere", Cornell_Box },
		{ 8, "cornell_smoke", "Cornell box with two constant media", Cornell_Smoke },
		{ 9, "chapter_two_final", "RTTNW final scene", Chapter_Two_Final_Scene },
		{ 10, "triangle_test", "Triangle area light", Triangle_Test },
		{ 11, "obj_test", "Model/dragon.obj (not in repository)", [] { return LoadObjScene("obj_test", "Model/dragon.obj", 80, Vector3(0, -5, 0)); } },
		{ 12, "teapot", "Model/teapot.obj", [] { return LoadObjScene("teapot", "Model/teapot.obj", 45, Vector3(0, 0, 0)); } },
		{ 13, "sponza", "Model/sponza.obj (not in repository)", [] { return LoadObjScene("sponza", "Model/sponza.obj", 45, Vector3(0, 0, 0)); } },
		{ 14, "pbr_test", "PBR ornament sphere (Model/sphere.obj, not in repository)", PBR_Test },
		{ 15, "pbr_benchmark", "PBR validation matrix (benchmark scene)", PBR_Benchmark },
		{ 16, "pbr_normal_map_test", "Normal map panels, with / without", PBR_Normal_Map_Test },
		{ 17, "obj_pbr_test", "OBJ/MTL -> PBR_Material automatic mapping", Obj_PBR_Test },
		{ 18, "pbr_ibl_test", "HDRI + area light + textured PBR sphere", PBR_IBL_Test },
		{ 19, "readme_showcase", "README showcase", README_Showcase },
		{ 100, "furnace_lambert", "Test: white Lambert sphere, uniform env (analytic 1)", Furnace_Lambert, true },
		{ 101, "furnace_pbr", "Test: white PBR metal/dielectric roughness sweep, uniform env", Furnace_PBR, true },
		{ 102, "furnace_volume", "Test: albedo-1 medium, uniform env (analytic 1)", Furnace_Volume, true },
		{ 103, "cornell_small", "Test: area light Cornell box", Cornell_Small, true },
		{ 104, "volume_small", "Test: media in a Cornell box", Volume_Small, true },
		{ 105, "normal_map_small", "Test: normal-mapped triangles and spheres", Normal_Map_Small, true },
		{ 106, "environment_small", "Test: HDR environment + area light", Environment_Small, true },
	};
	return registry;
}

const SceneEntry* find_scene(const std::string& id_or_name)
{
	const std::string key = to_lower(id_or_name);
	for (const SceneEntry& entry : scene_registry())
	{
		if (entry.name == key || std::to_string(entry.id) == key)
			return &entry;
	}
	return nullptr;
}
