#pragma once
#include<memory>
#include "Hittable.h"


class Quad : public Hittable
{
public :
	Quad(const Point3& Q, const Vector3& u, const Vector3& v, std::shared_ptr<Material> mat) :
		Q(Q), u(u), v(v), mat(mat) 
	{
		auto normal = cross(u, v);
		n = normalize(normal);
		D = dot(n, Q);
		w = normal / dot(normal, normal);

		set_bounding_box();
	}

	virtual void set_bounding_box()
	{
		auto bbox1 = AABB(Q, Q + u + v);
		auto bbox2 = AABB(Q + u, Q + v);
		bbox = AABB(bbox1, bbox2);
	}

	AABB bounding_box() const override { return bbox; }

	bool Hit(const Ray& ray, Interval& ray_t, HitRecord& rec) const override
	{
		// No hit if the ray is parallel to the plane
		auto denom = dot(n, ray.direction());
		if(std::fabs(denom) < 1e-8) return false;

		// Return false if the hit point parameter t is outside the ray interval
		auto t = (D - dot(n, ray.origin())) / denom;
		if (!(ray_t.contains(t))) return false;

		auto intersection = ray.at(t);
		// Determine whether the intersection point is in the effective area of the quad
		Vector3 PQ = intersection - Q;
		auto alpha = dot(w, cross(PQ, v));
		auto beta = dot(w, cross(u, PQ));

		if (!is_interior(alpha, beta, rec)) return false;

		rec.p = intersection;
		rec.t = t;
		rec.mat = mat;
		rec.set_face_front(ray, n);

		return true;
	}

	virtual bool is_interior(double a, double b, HitRecord& rec) const
	{
		Interval unit_interval(0, 1);
		if (!unit_interval.contains(a) || !unit_interval.contains(b)) return false;

		rec.u = a;
		rec.v = b;
		return true;
	}

private :
	Point3 Q;
	Vector3 u, v;
	Vector3 n;
	double D;
	Vector3 w;
	std::shared_ptr<Material> mat;
	AABB bbox;
};

