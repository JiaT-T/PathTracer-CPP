#pragma once
#include<chrono>
#include<iostream>
class Timer
{
public :
	// Get the current time
	Timer() : start_time(std::chrono::steady_clock::now()) {}

	double elapsed_seconds() const
	{
		auto end_time = stopped ? end_time_point : std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsed = end_time - start_time;
		return elapsed.count();
	}

	double stop()
	{
		if (!stopped)
		{
			end_time_point = std::chrono::steady_clock::now();
			stopped = true;

			std::clog << "\n[Profiler] Rendering finished in: "
				<< elapsed_seconds() << " seconds.\n";
		}

		return elapsed_seconds();
	}

	// Called automatically when leaving the scope
	// Then compute and print the total time
	~Timer()
	{
		if (!stopped)
			stop();
	}

private :
	std::chrono::time_point<std::chrono::steady_clock> start_time;
	std::chrono::time_point<std::chrono::steady_clock> end_time_point{};
	bool stopped = false;
};

