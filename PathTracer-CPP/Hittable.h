#pragma once
#include "AABB.h"

class Material;

class HitRecord
{
public :
	Point3 p;
	Vector3 n;
	double t;
	bool front_face;
	std::shared_ptr<Material> mat;
	double u;
	double v;

	// outward_normal is normalized
	void set_face_front(const Ray& ray, const Vector3& outward_normal)
	{
		front_face = dot(ray.direction(), outward_normal) < 0;
		n = front_face ? outward_normal : -outward_normal;
	}
};

class Hittable
{
public : 
	virtual ~Hittable() = default;
	virtual bool Hit(const Ray& ray, Interval& ray_t, HitRecord& rec) const = 0;
	virtual AABB bounding_box() const = 0;
};

