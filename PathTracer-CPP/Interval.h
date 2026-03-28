#pragma once
class Interval
{
public :
	Interval(double min, double max) : min(min), max(max) {};

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

private :
	double min;
	double max;
};

