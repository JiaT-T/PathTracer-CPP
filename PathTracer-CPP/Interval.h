#pragma once
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

	// Panding the boundary of the interval by delta, use for handling the grazing cases
	Interval expand(double delta) const
	{
		auto padding = delta / 2.0;
		return Interval(min - padding, max + padding);
	}

	double min;
	double max;
};

