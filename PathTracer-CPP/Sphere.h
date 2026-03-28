#pragma once
#include "Hittable.h"
class Sphere : public Hittable
{
public :
	Sphere(const Point3& center, double radius) : center(center), radius(std::fmax(0, radius)) {};

	bool Hit(const Ray& ray, double ray_tmin, double ray_tmax, HitRecord& rec) const override
	{
		Vector3 oc = center - ray.origin();
		auto a = ray.direction().length_squared();
		auto h = dot(ray.direction(), oc);
		auto c = oc.length_squared() - radius * radius;
		auto discriminant = h * h - a * c;

		if (discriminant < 0)
			return false;

		auto sqrtd = std::sqrt(discriminant);
		// Fristly, consider whether the closest intersection point can be clamped in the correct range
		auto root = (h - sqrtd) / a;
		if (root < ray_tmin || ray_tmax < root)
		{
			// If not, update the root to the other solution
			root = (h + sqrtd) / a;
			// Try again
			if (root < ray_tmin || ray_tmax < root)
				return false;  // If still not, which means absolutely cannot hit the effective area
		}

		rec.t = root;
		rec.p = ray.at(rec.t);
		Vector3 outward_normal = (rec.p - center) / radius;
		rec.set_face_front(ray, outward_normal);

		return true;
	}

private :
	Point3 center;
	double radius;
};

