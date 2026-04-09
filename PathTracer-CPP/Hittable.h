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
	virtual bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const = 0;
	virtual AABB bounding_box() const = 0;
	virtual double pdf_value(const Point3& origin, const Vector3& direction) const { return 0.0; }
	virtual Vector3 random(const Point3& origin) const { return Vector3(1.0, 0.0, 0.0); } 
};

class Translation : public Hittable
{
public :
	Translation(std::shared_ptr<Hittable> obj, const Vector3& off)
		: object(std::move(obj)), offset(off)
	{
		bbox = object->bounding_box() + offset;
	}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		Ray offset_ray(ray.origin() - offset, ray.direction(), ray.time());

		// Test the moved ray against the original object in object space.
		if (!object->Hit(offset_ray, ray_t, rec))
			return false;
		
		rec.p += offset;
		return true;
	}

	AABB bounding_box() const override { return bbox; }

private :
	std::shared_ptr<Hittable> object;
	Vector3 offset;
	AABB bbox;
};

class Rotate_Y : public Hittable
{
public :
	Rotate_Y(std::shared_ptr<Hittable> obj, double angle) : object(std::move(obj))
	{
		auto radians = degrees_to_radians(angle);

		sin_theta = std::sin(radians);
		cos_theta = std::cos(radians);
		bbox = object->bounding_box();

		Point3 min(infinity, infinity, infinity);
		Point3 max(-infinity, -infinity, -infinity);

		for (int i = 0; i < 2; i++)
		{
			for (int j = 0; j < 2; j++)
			{
				for (int k = 0; k < 2; k++)
				{
					auto x = i * bbox.x.max + (1 - i) * bbox.x.min;
					auto y = j * bbox.y.max + (1 - j) * bbox.y.min;
					auto z = k * bbox.z.max + (1 - k) * bbox.z.min;

					auto newx = cos_theta * x + sin_theta * z;
					auto newz = -sin_theta * x + cos_theta * z;

					Vector3 temp(newx, y, newz);

					for (int c = 0; c < 3; c++)
					{
						min[c] = std::fmin(min[c], temp[c]);
						max[c] = std::fmax(max[c], temp[c]);
					}
				}
			}
		}

		bbox = AABB(min, max);
	}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		auto origin = Point3(
			(cos_theta * ray.origin().x()) - (sin_theta * ray.origin().z()),
			ray.origin().y(),
			(sin_theta * ray.origin().x()) + (cos_theta * ray.origin().z()));

		auto direction = Vector3(
			(cos_theta * ray.direction().x()) - (sin_theta * ray.direction().z()),
			ray.direction().y(),
			(sin_theta * ray.direction().x()) + (cos_theta * ray.direction().z()));

		Ray rotated_ray(origin, direction, ray.time());

		if (!object->Hit(rotated_ray, ray_t, rec))
			return false;

		rec.p = Point3(
			(cos_theta * rec.p.x()) + (sin_theta * rec.p.z()),
			rec.p.y(),
			(-sin_theta * rec.p.x()) + (cos_theta * rec.p.z()));

		rec.n = Vector3(
			(cos_theta * rec.n.x()) + (sin_theta * rec.n.z()),
			rec.n.y(),
			(-sin_theta * rec.n.x()) + (cos_theta * rec.n.z()));

		return true;
	}

	AABB bounding_box() const override { return bbox; }

private :
	std::shared_ptr < Hittable> object;
	double sin_theta;
	double cos_theta;
	AABB bbox;
};
