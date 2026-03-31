#pragma once
#include<chrono>>
#include<iostream>
class Timer
{
public :
	// Get the current time
	Timer() { start_time = std::chrono::steady_clock::now(); }

	// Called automatically when leaving the scope
	// Then compute and print the total time
	~Timer()
	{
		auto end_time = std::chrono::steady_clock::now();
		// Compute the duration in seconds
		std::chrono::duration<double> elapsed_seconds = end_time - start_time;

		std::clog << "\n[Profiler] Rendering finished in: "
			<< elapsed_seconds.count() << " seconds.\n";
	}

private :
	std::chrono::time_point<std::chrono::steady_clock> start_time;
};

