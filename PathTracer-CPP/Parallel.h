#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Minimal dynamic-scheduling parallel-for with an explicit thread count.
//
// std::execution::par gives no control over the number of worker threads, which the scaling
// benchmark (1/2/4/8/16/all threads) and the determinism tests need. Work items are handed out
// through a single atomic counter (one fetch_add per tile), so load balancing is equivalent to
// the previous tile scheduling. The calling thread participates as worker 0.
namespace parallel
{
	inline int hardware_threads()
	{
		const unsigned n = std::thread::hardware_concurrency();
		return n == 0 ? 1 : static_cast<int>(n);
	}

	inline int resolve_thread_count(int requested)
	{
		return requested <= 0 ? hardware_threads() : requested;
	}

	// fn(item_index, worker_index)
	template <typename Fn>
	void parallel_for(int item_count, int thread_count, Fn&& fn)
	{
		if (item_count <= 0)
			return;

		const int workers = std::max(1, std::min(thread_count, item_count));
		if (workers == 1)
		{
			for (int i = 0; i < item_count; ++i)
				fn(i, 0);
			return;
		}

		std::atomic<int> next_item{ 0 };
		auto worker_main = [&](int worker_index)
		{
			for (;;)
			{
				const int item = next_item.fetch_add(1, std::memory_order_relaxed);
				if (item >= item_count)
					break;
				fn(item, worker_index);
			}
		};

		std::vector<std::thread> pool;
		pool.reserve(static_cast<size_t>(workers - 1));
		for (int w = 1; w < workers; ++w)
			pool.emplace_back(worker_main, w);
		worker_main(0);
		for (auto& thread : pool)
			thread.join();
	}
}
