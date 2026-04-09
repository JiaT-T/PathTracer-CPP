#pragma once
#include <memory>
#include "Hittable.h"
#include "Vector3.h"

class Triangle : public Hittable
{
public :
	Triangle(const Point3& v0, const Point3& v1, const Point3& v2, std::shared_ptr<Material> mat) : 
		v0(v0), v1(v1), v2(v2), mat(mat) 
	{
		auto edge1 = v1 - v0;
		auto edge2 = v2 - v0;
		n = normalize(cross(edge1, edge2));
	}
	virtual bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override;

	virtual AABB bounding_box() const
	{
		Point3 p_min(
			std::fmin(v0.x(), std::fmin(v1.x(), v2.x())),
			std::fmin(v0.y(), std::fmin(v1.y(), v2.y())),
			std::fmin(v0.z(), std::fmin(v1.z(), v2.z())));

		Point3 p_max(
			std::fmax(v0.x(), std::fmax(v1.x(), v2.x())),
			std::fmax(v0.y(), std::fmax(v1.y(), v2.y())),
			std::fmax(v0.z(), std::fmax(v1.z(), v2.z())));

		return AABB(p_min, p_max);
	}

private :
	Point3 v0, v1, v2;
	Vector3 n;
	std::shared_ptr<Material> mat;
};

bool Triangle::Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const
{
	const Vector3 edge1 = v1 - v0;
	const Vector3 edge2 = v2 - v0;
	const Vector3 h = cross(ray.direction(), edge2);
	const double det = dot(h, edge1);

	// If ray are parallel to the face, return false
	if (std::fabs(det) < 1e-7) return false;

	const double inv_det = 1.0 / det;

	const Vector3 ori_to_v0 = ray.origin() - v0;
	double u = dot(ori_to_v0, h) * inv_det;
	if (u < 0.0 || u > 1.0) return false;

	const Vector3 q = cross(ori_to_v0, edge1);
	double v = dot(ray.direction(), q) * inv_det;
	if (v < 0.0 || u + v > 1.0) return false;

	// The distance from intersection point to origin
	const double distance = dot(edge2, q) * inv_det;
	if (!ray_t.contains(distance)) return false;

	rec.t = distance;
	rec.p = ray.at(distance);
	rec.set_face_front(ray, n);
	rec.mat = mat;
	rec.u = u; 
	rec.v = v;

	return true;
}