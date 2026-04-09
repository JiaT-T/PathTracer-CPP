#pragma once
#include <vector>
#include <memory>
#include "Hittable.h"
#include "Interval.h"
#include "AABB.h"

class Hittable_List : public Hittable
{
public :
	std::vector<shared_ptr<Hittable>> objects;

	Hittable_List() {}
	Hittable_List(shared_ptr<Hittable> object) { add(object); }

	void clear() { objects.clear(); }

	void add(const shared_ptr<Hittable>& object)
	{
		objects.push_back(object);
		bbox = AABB(bbox, object->bounding_box());
	}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		HitRecord temp_rec;
		bool hitAnything = false;
		auto closest_so_far = ray_t.max;
		for (const auto& object : objects)
		{
			Interval hit_interval(ray_t.min, closest_so_far);
			if (object->Hit(ray, hit_interval, temp_rec))
			{
				hitAnything = true;
				closest_so_far = temp_rec.t;
				rec = temp_rec;
			}
		}
		return hitAnything;
	}

	AABB bounding_box() const override { return bbox; }

	double pdf_value(const Point3& origin, const Vector3& direction) const override
	{
		if (objects.empty()) return 0.0;

		auto weight = 1.0 / objects.size();
		auto sum = 0.0;
		for (const auto& object : objects)
			sum += weight * object->pdf_value(origin, direction);

		return sum;
	}

	Vector3 random(const Point3& origin) const override
	{
		auto size = static_cast<int>(objects.size());
		return objects[random_int(0, size - 1)]->random(origin);
	}

private :
	AABB bbox;
};

