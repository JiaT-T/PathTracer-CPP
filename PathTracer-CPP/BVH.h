#pragma once
#include <vector>
#include "Hittable_List.h"
#include "My_Common.h"
#include <algorithm>

class BVH_Node : public Hittable
{
public :
	BVH_Node(Hittable_List list) : BVH_Node(list.objects, 0, list.objects.size()) {}

	BVH_Node(std::vector<std::shared_ptr<Hittable>>& objects, size_t start, size_t end)
	{
		bbox = AABB::empty;
		for (size_t i = start; i < end; i++ )
		{
			bbox = AABB(bbox, objects[i]->bounding_box());
		}

		int axis = bbox.longest_axis();

		// The reason why those three functions are unparenthesized is
		// that they only need to be called in the std::sort, 
		// and the sort will call them with the correct parameters
		auto compartator = (axis == 0) ? box_compare_x :
						   (axis == 1) ? box_compare_y :
						   box_compare_z;
		size_t object_span = end - start;
		if (object_span == 1)
			left = right = objects[start];
		else if (object_span == 2)
		{
			left = objects[start];
			right = objects[start + 1];
		}
		else
		{
			// Sort the objects according to the selected axis, and then split them into two halves
			// Let the left half be the left child of the BVH node, 
			// and the right half be the right child of the BVH node
			std::sort(std::begin(objects) + start, std::begin(objects) + end, compartator);
			auto middle = start + object_span / 2;

			left = std::make_shared<BVH_Node>(objects, start, middle);
			right = std::make_shared<BVH_Node>(objects, middle, end);
		}
	}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		if (!bbox.hit(ray, ray_t))
			return false;

		bool hit_left = left->Hit(ray, ray_t, rec);
		Interval hit_interval_right(ray_t.min, hit_left ? rec.t : ray_t.max);
		bool hit_right = right->Hit(ray, hit_interval_right, rec);

		return hit_left || hit_right;			
	}

	AABB bounding_box() const override { return bbox; }

private :
	std::shared_ptr<Hittable> left;
	std::shared_ptr<Hittable> right;
	AABB bbox;

	static bool box_compare(const std::shared_ptr<Hittable>& a, const std::shared_ptr<Hittable>& b, int axisIndex)
	{
		auto a_interval = a->bounding_box().axis_interval(axisIndex);
		auto b_interval = b->bounding_box().axis_interval(axisIndex);
		return a_interval.min < b_interval.min;
	}

	static bool box_compare_x(const std::shared_ptr<Hittable>& a, const std::shared_ptr<Hittable>& b)
	{
		return box_compare(a, b, 0);
	}

	static bool box_compare_y(const std::shared_ptr<Hittable>& a, const std::shared_ptr<Hittable>& b)
	{
		return box_compare(a, b, 1);
	}

	static bool box_compare_z(const std::shared_ptr<Hittable>& a, const std::shared_ptr<Hittable>& b)
	{
		return box_compare(a, b, 2);
	}
};

