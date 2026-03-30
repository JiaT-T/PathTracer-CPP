#pragma once
#include "Vector3.h"

class Ray
{
public :
	Ray() {};
	Ray(const Point3& ori, const Vector3& direction, double tm) : orig(ori), dir(direction), t(tm) {}
	Ray(const Point3& ori, const Vector3& direction) : Ray(ori, direction, 0.0) {}

	const Point3& origin() const { return orig; }
	const Vector3& direction() const { return dir; }
	const double time() const { return t; }

	Point3 at(double t) const
	{
		return orig + t * dir;
	}

private :
	Point3 orig;
	Vector3 dir;
	double t;
};

