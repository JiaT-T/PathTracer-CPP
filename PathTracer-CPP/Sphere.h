#pragma once
#include "ONB.h"
#include "Hittable.h"
class Sphere : public Hittable
{
public :
	// Static Sphere
	Sphere(const Point3& static_center, double radius, std::shared_ptr<Material> mat) :
		center(Ray(static_center, Vector3(0, 0, 0))), radius(std::fmax(0, radius)), mat(mat) 
	{
		auto rVec3 = Vector3(radius, radius, radius);
		// Create a bounding box for the static sphere,
		// So that this sphere becomes his incisive sphere
		bbox = AABB(static_center - rVec3, static_center + rVec3);
	};

	// Moving Sphere
	Sphere(const Point3& center1, const Point3& center2, double radius, std::shared_ptr<Material> mat) :
		center(center1, center2 - center1), radius(std::fmax(0, radius)), mat(mat) 
	{
		auto rVec3 = Vector3(radius, radius, radius);
		AABB box1(center1 - rVec3, center1 + rVec3);
		AABB box2(center2 - rVec3, center2 + rVec3);
		bbox = AABB(box1, box2);
	};

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		Point3 curr_center = center.at(ray.time());
		Vector3 oc = curr_center - ray.origin();
		auto a = ray.direction().length_squared();
		auto h = dot(ray.direction(), oc);
		auto c = oc.length_squared() - radius * radius;
		auto discriminant = h * h - a * c;

		if (discriminant < 0)
			return false;

		auto sqrtd = std::sqrt(discriminant);
		// Fristly, consider whether the closest intersection point can be clamped in the correct range
		auto root = (h - sqrtd) / a;
		if (root < ray_t.min || ray_t.max < root)
		{
			// If not, update the root to the other solution
			root = (h + sqrtd) / a;
			// Try again
			if (root < ray_t.min || ray_t.max < root)
				return false;  // If still not, which means absolutely cannot hit the effective area
		}

		rec.t = root;
		rec.p = ray.at(rec.t);
		Vector3 outward_normal = (rec.p - curr_center) / radius;
		rec.set_face_front(ray, outward_normal);
		rec.mat = mat;
		get_sphere_uv(outward_normal, rec.u, rec.v);

		return true;
	}

	AABB bounding_box() const override { return bbox; }

	void get_sphere_uv(const Point3& p, double& u, double& v) const
	{
		auto theta = std::acos(-p.y());
		auto phi = std::atan2(-p.z(), p.x()) + pi;
		u = phi / (2.0 * pi);
		v = theta / pi;
	}

	double pdf_value(const Point3& origin, const Vector3& direction) const override
	{
		HitRecord rec;
		if (!this->Hit(Ray(origin, direction), Interval(0.001, infinity), rec)) return 0;

		auto squared_distanced = (center.at(0) - origin).length_squared();
		auto max_cosine_theta = std::sqrt(1 - radius * radius / squared_distanced);
		auto solid_angle = 2.0 * pi * (1.0 - max_cosine_theta);

		return 1.0 / solid_angle;
	}

	Vector3 random(const Point3& origin) const override
	{
		Vector3 dir = center.at(0) - origin;
		double squared_distanced = dir.length_squared();
		ONB uvw(dir);

		return uvw.transform(random_to_sphere(radius, squared_distanced));
	}

private :
	Ray center;
	double radius;
	std::shared_ptr<Material> mat;
	AABB bbox;

	static Vector3 random_to_sphere(double radius, double squared_distance)
	{
		auto r1 = random_double();
		auto r2 = random_double();
		auto z = 1 + r2 * (std::sqrt(1 - radius * radius / squared_distance) - 1);

		auto phi = 2 * pi * r1;
		auto x = std::cos(phi) * std::sqrt(1 - z * z);
		auto y = std::sin(phi) * std::sqrt(1 - z * z);

		return Vector3(x, y, z);
	}
};