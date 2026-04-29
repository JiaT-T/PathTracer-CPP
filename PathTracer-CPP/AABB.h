#pragma once
#include "My_Common.h"	
#include "Interval.h"
#include "Vector3.h"
#include "Ray.h"
class AABB
{
public :
	Interval x, y, z;

	AABB() {}

	AABB(const Interval& x, const Interval& y, const Interval& z) : x(x), y(y), z(z) 
	{
		pad_to_minimums();
	}

	// Input two points, which are the opposite corners of the box, and construct the AABB
	// You may ask: Why a coordinate value can build the boudary of the box?
	// The reason is: The face where the coordinate value located in is the boundary of the box
	// For example, if the x value is defined, then the face is the YoZ plane where the x value is the same as the coordinate value 
	AABB(const Point3& a, const Point3& b)
	{
		x = (a[0] <= b[0]) ? Interval(a[0], b[0]) : Interval(b[0], a[0]);
		y = (a[1] <= b[1]) ? Interval(a[1], b[1]) : Interval(b[1], a[1]);
		z = (a[2] <= b[2]) ? Interval(a[2], b[2]) : Interval(b[2], a[2]);
	}

	AABB(const AABB& box1, const AABB& box2)
	{
		x = Interval(box1.x, box2.x);
		y = Interval(box1.y, box2.y);
		z = Interval(box1.z, box2.z);

		pad_to_minimums();
	}

	const Interval& axis_interval(int axis) const
	{
		if (axis == 0) return x;
		if (axis == 1) return y;
		return z;
	}

	bool hit(const Ray& ray, Interval ray_t) const
	{
		const Point3& origin = ray.origin();
		const Vector3& direction = ray.direction();

		for (int axis = 0; axis < 3; axis++)
		{
			const Interval& ax = axis_interval(axis);
			const double axDirInv = 1.0 / direction[axis];

			auto t0 = (ax.min - origin[axis]) * axDirInv;
			auto t1 = (ax.max - origin[axis]) * axDirInv;

			// If the ray is pointing to the negative direction of the axis, 
			// then swap t0 and t1, 
			// which means that the ray will hit the max face of the box before hitting the min face
			if (axDirInv < 0) std::swap(t0, t1);

			// Update the ray_t according to the intersection with the current axis-aligned face of the box
			if (t0 > ray_t.min) ray_t.min = t0;
			if (t1 < ray_t.max) ray_t.max = t1;

			if (ray_t.max <= ray_t.min) return false;
		}
		return true;
	}

	// Returns the index of the longest axis of the bounding box
	int longest_axis() const
	{
		if (x.size() < y.size()) return y.size() < z.size() ? 2 : 1;
		else return x.size() < z.size() ? 2 : 0;
	}

	Point3 min() const
	{
		return Point3(x.min, y.min, z.min);
	}

	Point3 max() const
	{
		return Point3(x.max, y.max, z.max);
	}

	static const AABB empty, universe;

private :
	void pad_to_minimums()
	{
		// Ensure that the size of the box is not too small, which can cause numerical issues in ray-box intersection tests
		double delta = 0.0001;
		if (x.size() < delta) x = x.expand(delta);
		if (y.size() < delta) y = y.expand(delta);
		if (z.size() < delta) z = z.expand(delta);
	}
};

const AABB AABB::empty = AABB(Interval(infinity, -infinity), Interval(infinity, -infinity), Interval(infinity, -infinity));
const AABB AABB::universe = AABB(Interval(-infinity, infinity), Interval(-infinity, infinity), Interval(-infinity, infinity));

AABB operator+(const AABB& box, const Vector3& offset)
{
	return AABB(box.x + offset.x(), box.y + offset.y(), box.z + offset.z());
}

AABB operator+(const Vector3& offset, const AABB& box)
{
	return box + offset;
}