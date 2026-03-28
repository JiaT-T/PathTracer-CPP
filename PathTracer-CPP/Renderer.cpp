
#include "My_Common.h"
#include "Hittable.h"
#include "Hittable_List.h"
#include "Sphere.h"
#include "Camera.h"

int main()
{
	// Create World
	Hittable_List world;
	world.add(make_shared<Sphere>(Point3(0, 0, -1), 0.5));
	world.add(make_shared<Sphere>(Point3(0, -100.5, -1), 100));

	// Create Camera
	Camera cam;
	cam.aspect_ratio = 16.0 / 9.0;
	cam.image_width = 400;
	cam.sample_per_pixel = 100;
	cam.max_depth = 50;

	// Rendering
	cam.Render(world);
}