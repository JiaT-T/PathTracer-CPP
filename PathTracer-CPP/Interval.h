#pragma once
#include <cmath>
class Interval
{
public :
	Interval() = default;
	Interval(double min, double max) : min(min), max(max) {};
	Interval(const Interval& a, const Interval& b) : min(std::fmin(a.min, b.min)), max(std::fmax(a.max, b.max)) {}

	bool Surrounds(double x) const
	{
		return min < x && x < max;
	}

	double Clamp(double x) const
	{
		if (x < min) return min;
		if (max < x) return max;
		return x;
	}

	bool contains(const double t) const
	{
		return min <= t && t <= max;
	}

	// Panding the boundary of the interval by delta,
	// use for handling the grazing cases
	Interval expand(double delta) const
	{
		auto padding = delta / 2.0;
		return Interval(min - padding, max + padding);
	}

	double size() const { return max - min; }

	double min;
	double max;

	static const Interval universe;
};

Interval operator+(const Interval& a, double b)
{
	return Interval(a.min + b, a.max + b);
}

Interval operator+(double b, const Interval& a)
{
	return a+ b;
}

const Interval Interval::universe = Interval(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());

