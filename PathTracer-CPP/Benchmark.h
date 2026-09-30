#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

struct BenchmarkOptions
{
	std::string scene = "pbr_benchmark";
	int spp = -1;      // -1 = scene default
	int width = -1;
	int depth = -1;
	uint64_t seed = 1;
	std::vector<int> thread_list = { 1, 2, 4, 8, 16, 0 }; // 0 = all hardware threads
	int repeat = 1;    // best-of-N per thread count
};

inline std::vector<int> parse_int_list(const std::string& text)
{
	std::vector<int> values;
	std::stringstream stream(text);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		if (!item.empty())
			values.push_back(std::stoi(item));
	}
	return values;
}

int run_benchmark(const BenchmarkOptions& options);
