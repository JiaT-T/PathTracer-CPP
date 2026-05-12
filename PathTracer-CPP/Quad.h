#pragma once
#include<memory>
#include "Hittable.h"
#include "Hittable_List.h"

class Quad : public Hittable
{
public :
	Quad(const Point3& Q, const Vector3& u, const Vector3& v, std::shared_ptr<Material> mat) :
		Q(Q), u(u), v(v), mat(mat) 
	{
		auto normal = cross(u, v);
		// The length of normal is equal to the area of parallelogram of u and v
		area = normal.length();
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

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
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

	double pdf_value(const Point3& origin, const Vector3& direction) const override
	{
		HitRecord rec;
		if (!this->Hit(Ray(origin, direction), Interval(0.0001, infinity), rec)) return 0;

		auto squared_distance = (rec.t * rec.t) * direction.length_squared();
		auto cosine = std::fabs(dot(rec.n, direction) / direction.length());

		return squared_distance / (cosine * area);
	}

	Vector3 random(const Point3& origin) const override
	{
		auto p = Q + (random_double() * u) + (random_double() * v);
		return p - origin;
	}

	double sampling_power_estimate() const override
	{
		if (!mat)
			return 0.0;

		const Point3 center = Q + 0.5 * u + 0.5 * v;
		const double emitted_luminance = mat->EmissionLuninance(0.5, 0.5, center);

		// The power of the light emitted from the quad
		return emitted_luminance * area;
	}

private :
	Point3 Q;
	Vector3 u, v;
	Vector3 n;
	double D;
	Vector3 w;
	double area;
	std::shared_ptr<Material> mat;
	AABB bbox;
};

// Input two opposite vertices of the box, 
// and construct the box with 6 quads
inline std::shared_ptr<Hittable_List> Box(const Point3& a, const Point3& b, std::shared_ptr<Material> mat)
{
	auto sides = make_shared<Hittable_List>();

	// Construct the two opposite vertices with the minimum and maximum coordinates.
	auto min = Point3(std::fmin(a.x(), b.x()), std::fmin(a.y(), b.y()), std::fmin(a.z(), b.z()));
	auto max = Point3(std::fmax(a.x(), b.x()), std::fmax(a.y(), b.y()), std::fmax(a.z(), b.z()));
	
	auto dx = Vector3(max.x() - min.x(), 0, 0);
	auto dy = Vector3(0, max.y() - min.y(), 0);
	auto dz = Vector3(0, 0, max.z() - min.z());

	sides->add(make_shared<Quad>(Point3(min.x(), min.y(), max.z()), dx, dy, mat));  // front
	sides->add(make_shared<Quad>(Point3(max.x(), min.y(), max.z()), -dz, dy, mat)); // right
	sides->add(make_shared<Quad>(Point3(max.x(), min.y(), min.z()), -dx, dy, mat)); // back
	sides->add(make_shared<Quad>(Point3(min.x(), min.y(), min.z()), dz, dy, mat));  // left
	sides->add(make_shared<Quad>(Point3(min.x(), max.y(), max.z()), dx, -dz, mat)); // top
	sides->add(make_shared<Quad>(Point3(min.x(), min.y(), min.z()), dx, dz, mat));  // bottom

	return sides;
}
