#pragma once
#include<array>
#include "Vector3.h"
class ONB
{
public :
	ONB(const Vector3& n)
	{
		axis[2] = normalize(n);
		Vector3 a = (std::fabs(axis[2].x()) > 0.9) ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
		axis[1] = normalize(cross(axis[2], a));
		axis[0] = cross(axis[2], axis[1]);
	}

	Vector3 transform(const Vector3& v) const
	{
		// Transform from basis coordinates to local space.
		return (v[0] * axis[0]) + (v[1] * axis[1]) + (v[2] * axis[2]);
	}

	const Vector3& u() const { return axis[0]; }
	const Vector3& v() const { return axis[1]; }
	const Vector3& w() const { return axis[2]; }

private :
	std::array<Vector3, 3> axis;
};

