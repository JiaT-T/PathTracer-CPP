#pragma once
#include <memory>
#include "Hittable.h"
#include "Vector3.h"

struct TexCoord2
{
	double x = 0.0;
	double y = 0.0;

	TexCoord2() = default;
	TexCoord2(double x, double y) : x(x), y(y) {}
};

class Triangle : public Hittable
{
public :
	// Flat Shading
	Triangle(const Point3& v0, const Point3& v1, const Point3& v2, std::shared_ptr<Material> mat) : 
		Triangle(v0, v1, v2, TexCoord2(), TexCoord2(), TexCoord2(), std::move(mat))
	{
	}

	// Flat Shading with UV
	Triangle(const Point3& v0, const Point3& v1, const Point3& v2,
			 const TexCoord2& t0, const TexCoord2& t1, const TexCoord2& t2,
			 std::shared_ptr<Material> mat) :
		v0(v0), v1(v1), v2(v2), uv0(t0), uv1(t1), uv2(t2), mat(std::move(mat)), has_vertex_normal(false)
	{
		auto edge1 = v1 - v0;
		auto edge2 = v2 - v0;
		auto cross_product = cross(edge1, edge2);
		auto length = cross_product.length();
		area = 0.5 * length;
		face_normal = normalize(cross_product);
		n0 = n1 = n2 = face_normal;
		set_bounding_box();
	}

	// Smooth Shading without UV
	Triangle(const Point3& v0, const Point3& v1, const Point3& v2,
			 const Vector3& n0, const Vector3& n1, const Vector3& n2,
			 std::shared_ptr<Material> mat) :
		Triangle(v0, v1, v2, n0, n1, n2,
				 TexCoord2(), TexCoord2(), TexCoord2(),
				 std::move(mat))
	{
	}

	// Smooth Shading
	Triangle(const Point3& v0, const Point3& v1, const Point3& v2, 
			 const Vector3& n0, const Vector3& n1, const Vector3& n2, 
			 const TexCoord2& t0, const TexCoord2& t1, const TexCoord2& t2,
			 std::shared_ptr<Material> mat) :
			 v0(v0), v1(v1), v2(v2), n0(n0), n1(n1), n2(n2), uv0(t0), uv1(t1), uv2(t2),
			 mat(std::move(mat)), has_vertex_normal(true)
	{
		auto edge1 = v1 - v0;
		auto edge2 = v2 - v0;
		auto cross_product = cross(edge1, edge2);
		auto length = cross_product.length();
		area = 0.5 * length;
		face_normal = cross_product / length;

		set_bounding_box();
	}

	virtual bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override;

	virtual AABB bounding_box() const override { return bbox; }

	virtual void set_bounding_box()
	{
		Point3 p_min(
			std::fmin(v0.x(), std::fmin(v1.x(), v2.x())),
			std::fmin(v0.y(), std::fmin(v1.y(), v2.y())),
			std::fmin(v0.z(), std::fmin(v1.z(), v2.z())));

		Point3 p_max(
			std::fmax(v0.x(), std::fmax(v1.x(), v2.x())),
			std::fmax(v0.y(), std::fmax(v1.y(), v2.y())),
			std::fmax(v0.z(), std::fmax(v1.z(), v2.z())));

		bbox = AABB(p_min, p_max);
	}

	double pdf_value(const Point3& origin, const Vector3& direction) const override
	{
		HitRecord rec;
		if (!this->Hit(Ray(origin, direction), Interval(0.0001, infinity), rec)) return 0;

		auto squared_distance = (rec.t * rec.t) * direction.length_squared();
		auto cosine = std::fabs(dot(face_normal, direction) / direction.length());

		return squared_distance / (cosine * area);
	}

	Vector3 random(const Point3& origin) const override
	{
		auto sqrted_r1 = std::sqrt(random_double(0, 1));
		auto r2 = random_double(0, 1);

		auto alpha = 1.0 - sqrted_r1;
		auto beta = sqrted_r1 * (1.0 - r2);
		auto gamma = sqrted_r1 * r2;

		auto random_point = alpha * v0 + beta * v1 + gamma * v2;
		return random_point - origin;
	}

private :
	Point3 v0, v1, v2;
	Vector3 face_normal;
	Vector3 n0, n1, n2;
	TexCoord2 uv0, uv1, uv2;
	bool has_vertex_normal;
	std::shared_ptr<Material> mat;
	AABB bbox;
	double area;
};

inline bool Triangle::Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const
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

	Vector3 shading_normal;
	if (has_vertex_normal)
	{
		double w = 1.0 - u - v;
		shading_normal = w * n0 + u * n1 + v * n2;

		if (shading_normal.length_squared() < 1e-8)
			shading_normal = face_normal;
		else
			shading_normal = normalize(shading_normal);
	}
	else
	{
		shading_normal = face_normal;
	}

	if (dot(ray.direction(), face_normal) < 0)
		rec.p += face_normal * 0.001;
	else
		rec.p -= face_normal * 0.001;

	// Texture coordinates interpolation
	double w = 1.0 - u - v;
	rec.u = w * uv0.x + u * uv1.x + v * uv2.x;
	rec.v = w * uv0.y + u * uv1.y + v * uv2.y;

	rec.mat = mat;
	rec.set_face_front(ray, shading_normal);

	return true;
}
