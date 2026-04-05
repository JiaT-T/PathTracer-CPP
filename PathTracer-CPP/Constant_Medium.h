#pragma once
#include "Hittable.h"
#include "Material.h"
#include "Texture.h"

class Constant_Medium : public Hittable
{
public :
	Constant_Medium(std::shared_ptr<Hittable> boundary, double density, std::shared_ptr<Texture> albedo) :
		boundary(std::move(boundary)), neg_inv_density(-1.0 / density), phase_function(std::make_shared<isotropic>(albedo)) {}
	Constant_Medium(std::shared_ptr<Hittable> boundary, double density, const Color& albedo) :
		boundary(std::move(boundary)), neg_inv_density(-1.0 / density), phase_function(std::make_shared<isotropic>(albedo)) {}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		HitRecord rec1, rec2;

		// Find the entery
		if (!boundary->Hit(ray, Interval::universe, rec1))
			return false;
		// Find the exit
		if (!boundary->Hit(ray, Interval(rec1.t + 0.0001, infinity), rec2))
			return false;

		// Clamp to the effective interval
		if (rec1.t < ray_t.min) rec1.t = ray_t.min;
		if (rec2.t > ray_t.max) rec2.t = ray_t.max;
		if (rec1.t >= rec2.t) return false;
		if (rec1.t < 0) rec1.t = 0;

		// Compute the total distance the ray travels in the boundary
		auto length = ray.direction().length();
		auto distance_inside_boundary = (rec2.t - rec1.t) * length;
		auto hit_distance = neg_inv_density * std::log(random_double());

		// Whether the ray hits a scattering point in the medium
		if (hit_distance > distance_inside_boundary)
			return false;

		// Record the hit point information
		rec.t = rec1.t + hit_distance / length;
		rec.p = ray.at(rec.t);

		rec.n = Vector3(1, 0, 0);  // arbitrary
		rec.front_face = true;     // also arbitrary
		rec.mat = phase_function;

		return true;
	}

	AABB bounding_box() const override { return boundary->bounding_box(); }

private :
	std::shared_ptr<Hittable> boundary;
	double neg_inv_density;
	std::shared_ptr<Material> phase_function;
};

