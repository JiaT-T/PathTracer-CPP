// Global allocation counter (Audit P-1 verification).
//
// Replaces the global non-aligned operator new / delete with malloc / free plus one relaxed
// atomic increment. The hot path of the renderer is expected to perform zero allocations per
// camera ray; tests and the benchmark read heap_allocation_count() before and after rendering.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace
{
	std::atomic<uint64_t> g_allocations{ 0 };

	void* counted_alloc(std::size_t size)
	{
		g_allocations.fetch_add(1, std::memory_order_relaxed);
		if (size == 0)
			size = 1;
		if (void* p = std::malloc(size))
			return p;
		throw std::bad_alloc();
	}
}

uint64_t heap_allocation_count()
{
	return g_allocations.load(std::memory_order_relaxed);
}

void* operator new(std::size_t size) { return counted_alloc(size); }
void* operator new[](std::size_t size) { return counted_alloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	g_allocations.fetch_add(1, std::memory_order_relaxed);
	return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	g_allocations.fetch_add(1, std::memory_order_relaxed);
	return std::malloc(size == 0 ? 1 : size);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
