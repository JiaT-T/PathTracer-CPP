#pragma once
#include<array>
#include <cmath>

double random_double();		
double random_double(double min, double max);

class Vector3
{
public:
	std::array<double, 3> e;

	Vector3() : e{ 0,0,0 } {};
	Vector3(double e0, double e1, double e2) : e{ e0, e1,e2 } {};

	double x() const { return e[0]; }
	double y() const { return e[1]; }
	double z() const { return e[2]; }

	Vector3 operator-() const { return { -e[0], -e[1], -e[2] }; }
	double& operator[](int i) { return e[i]; }
	double operator[] (int i) const { return e[i]; }

	Vector3& operator+=(const Vector3& v)
	{
		e[0] += v[0];
		e[1] += v[1];
		e[2] += v[2];
		return *this;
	}

	Vector3& operator-=(const Vector3& v)
	{
		e[0] -= v[0];
		e[1] -= v[1];
		e[2] -= v[2];
		return *this;
	}

	Vector3& operator*=(const Vector3& v)
	{
		e[0] *= v[0];
		e[1] *= v[1];
		e[2] *= v[2];
		return *this;
	}

	Vector3& operator/=(const Vector3& v)
	{
		e[0] /= v[0];
		e[1] /= v[1];
		e[2] /= v[2];
		return *this;
	}

	Vector3& operator*=(double d)
	{
		e[0] *= d;
		e[1] *= d;
		e[2] *= d;
		return *this;
	}

	Vector3& operator/=(double d)
	{
		return *this *= (1.0 / d);
	}

	double length() const
	{
		return std::sqrt(length_squared());
	}

	double length_squared() const
	{
		return e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
	}

	bool near_zero() const
	{
		auto s = 1e-8;
		return (std::fabs(e[0]) < s) && (std::fabs(e[1]) < s) && (std::fabs(e[2]) < s);
	}

	static Vector3 random(double a, double b);
};
using Point3 = Vector3;

// Vector3 With Vector3
inline Vector3 operator+(const Vector3& u, const Vector3& v)
{
	return Vector3(u[0] + v[0], u[1] + v[1], u[2] + v[2]);
}

inline Vector3 operator-(const Vector3& u, const Vector3& v)
{
	return Vector3(u[0] - v[0], u[1] - v[1], u[2] - v[2]);
}

inline Vector3 operator*(const Vector3& u, const Vector3& v)
{
	return Vector3(u[0] * v[0], u[1] * v[1], u[2] * v[2]);
}

inline Vector3 operator/(const Vector3& u, const Vector3& v)
{
	return Vector3(u[0] / v[0], u[1] / v[1], u[2] / v[2]);
}

inline double dot(const Vector3& u, const Vector3& v)
{
	return (u[0] * v[0] + u[1] * v[1] + u[2] * v[2]);
}

inline Vector3 cross(const Vector3& u, const Vector3& v)
{
	return Vector3(u[1] * v[2] - u[2] * v[1],
				   u[2] * v[0] - u[0] * v[2],
				   u[0] * v[1] - u[1] * v[0] );
}

// Vector3 With Constant
inline Vector3 operator+(const Vector3& u, double d)
{
	return Vector3(u[0] + d, u[1] + d, u[2] + d);
}

inline Vector3 operator+(double d, const Vector3& u)
{
	return u + d;
}

inline Vector3 operator-(const Vector3& u, double d)
{
	return Vector3(u[0] - d, u[1] - d, u[2] - d);
}

inline Vector3 operator*(const Vector3& u, double d)
{
	return Vector3(u[0] * d, u[1] * d, u[2] * d);
}

inline Vector3 operator*(double d, const Vector3& u)
{
	return u * d;
}

inline Vector3 operator/(const Vector3& u, double d)
{
	return u * (1.0 / d);
}

inline Vector3 normalize(const Vector3& u)
{
	return u / u.length();
}

inline Point3 random_in_unit_disk()
{
	while (true)
	{
		auto p = Vector3(random_double(-1, 1), random_double(-1, 1), 0);
		if (p.length_squared() < 1)
			return p;
	}
}

inline Vector3 Point3::random(double a, double b)
{
	return Point3(random_double(a, b), random_double(a, b), random_double(a, b));
}

inline Vector3 random_cosine_dir()
{
	auto r1 = random_double();
	auto r2 = random_double();

	auto phi = 2 * pi * r1;
	auto x = std::cos(phi) * std::sqrt(r2);
	auto y = std::sin(phi) * std::sqrt(r2);
	auto z = std::sqrt(1 - r2);

	return Vector3(x, y, z);
}