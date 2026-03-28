#pragma once
#include <vector>
#include <memory>
#include "Hittable.h"

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
	}

	bool Hit(const Ray& ray, double ray_tmin, double ray_tmax, HitRecord& rec) const override
	{
		HitRecord temp_rec;
		bool hitAnything = false;
		auto closest_so_far = ray_tmax;

		for (const auto& object : objects)
		{
			if (object->Hit(ray, ray_tmin, closest_so_far, temp_rec))
			{
				hitAnything = true;
				closest_so_far = temp_rec.t;
				rec = temp_rec;
			}
		}
		return hitAnything;
	}
};

