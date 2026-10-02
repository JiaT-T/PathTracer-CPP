#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "Hittable_List.h"

// Binary BVH using a 16-bin surface-area heuristic on all three centroid axes.
// Construction caches primitive bounds once and never consumes the renderer RNG.
class BVH_Node : public Hittable
{
public:
	explicit BVH_Node(const Hittable_List& list)
		: BVH_Node(list.objects, 0, list.objects.size()) {}

	BVH_Node(const std::vector<std::shared_ptr<Hittable>>& objects, size_t start, size_t end)
	{
		std::vector<BuildPrimitive> primitives;
		primitives.reserve(end - start);
		for (size_t i = start; i < end; ++i)
		{
			const AABB bounds = objects[i]->bounding_box();
			primitives.push_back({ objects[i], bounds, 0.5 * bounds.min() + 0.5 * bounds.max(), i });
		}
		build(primitives, 0, primitives.size(), 0);
	}

	bool Hit(const Ray& ray, Interval ray_t, HitRecord& rec) const override
	{
		bool found = false;
		size_t hit_index = 0;
		hit_nearest(ray, ray_t, rec, found, hit_index);
		return found;
	}

	AABB bounding_box() const override { return bbox; }

private:
	static constexpr int kBinCount = 16;
	static constexpr size_t kMaxLeafSize = 4;
	static constexpr int kMaxBuildDepth = 64;
	struct BuildPrimitive
	{
		std::shared_ptr<Hittable> object;
		AABB bounds;
		Point3 centroid;
		size_t index;
	};
	struct LeafPrimitive
	{
		std::shared_ptr<Hittable> object;
		size_t index;
	};
	struct Bin
	{
		AABB bounds;
		size_t count = 0;
	};

	std::unique_ptr<BVH_Node> left, right;
	std::vector<LeafPrimitive> leaf;
	AABB bbox;
	int split_axis = 0;

	BVH_Node() = default;

	void hit_nearest(const Ray& ray, Interval ray_t, HitRecord& rec, bool& found, size_t& hit_index) const
	{
		PT_COUNT_BVH_NODE();
		if (!bbox.hit(ray, ray_t)) return;
		if (!left)
		{
			HitRecord candidate;
			for (const auto& primitive : leaf)
			{
				if (primitive.object->Hit(ray, ray_t, candidate) &&
					(!found || candidate.t < rec.t || (candidate.t == rec.t && primitive.index > hit_index)))
				{
					found = true;
					ray_t.max = candidate.t;
					rec = candidate;
					hit_index = primitive.index;
				}
			}
			return;
		}

		// Lower centroid bins go left. Direction ordering is a cheap near-side
		// heuristic, not an exact ordering of overlapping child bounding boxes.
		const bool reverse = ray.direction()[split_axis] < 0.0;
		const BVH_Node* first = reverse ? right.get() : left.get();
		const BVH_Node* second = reverse ? left.get() : right.get();
		first->hit_nearest(ray, ray_t, rec, found, hit_index);
		if (found) ray_t.max = rec.t;
		// Keep t_min unchanged (medium entry/exit queries may start behind the ray).
		// Equal-distance hits remain eligible; later input primitives win ties,
		// matching Hittable_List independently of the tree's traversal order.
		second->hit_nearest(ray, ray_t, rec, found, hit_index);
	}

	static int bin_index(double centroid, double minimum, double extent)
	{
		return std::clamp(static_cast<int>(kBinCount * ((centroid - minimum) / extent)), 0, kBinCount - 1);
	}

	void make_leaf(const std::vector<BuildPrimitive>& primitives, size_t start, size_t end)
	{
		leaf.reserve(end - start);
		for (size_t i = start; i < end; ++i)
			leaf.push_back({ primitives[i].object, primitives[i].index });
	}

	void build(std::vector<BuildPrimitive>& primitives, size_t start, size_t end, int depth)
	{
		const size_t count = end - start;
		if (count == 0) return;
		Point3 centroid_min(infinity, infinity, infinity);
		Point3 centroid_max(-infinity, -infinity, -infinity);
		bool finite_centroids = true;
		for (size_t i = start; i < end; ++i)
		{
			bbox = AABB(bbox, primitives[i].bounds);
			for (int axis = 0; axis < 3; ++axis)
			{
				const double c = primitives[i].centroid[axis];
				finite_centroids = finite_centroids && std::isfinite(c);
				centroid_min[axis] = std::min(centroid_min[axis], c);
				centroid_max[axis] = std::max(centroid_max[axis], c);
			}
		}
		if (count == 1 || depth >= kMaxBuildDepth)
		{
			make_leaf(primitives, start, end);
			return;
		}

		const double parent_area = bbox.surface_area();
		double best_cost = infinity;
		int best_axis = -1, best_bin = -1;
		for (int axis = 0; axis < 3 && finite_centroids && parent_area > 0.0 && std::isfinite(parent_area); ++axis)
		{
			const double extent = centroid_max[axis] - centroid_min[axis];
			if (!(extent > 0.0) || !std::isfinite(extent)) continue;
			std::array<Bin, kBinCount> bins;
			for (size_t i = start; i < end; ++i)
			{
				Bin& bin = bins[bin_index(primitives[i].centroid[axis], centroid_min[axis], extent)];
				++bin.count;
				bin.bounds = AABB(bin.bounds, primitives[i].bounds);
			}

			std::array<double, kBinCount - 1> left_area_count;
			std::array<size_t, kBinCount - 1> left_count;
			AABB bounds;
			size_t n = 0;
			for (int b = 0; b < kBinCount - 1; ++b)
			{
				if (bins[b].count) bounds = AABB(bounds, bins[b].bounds);
				n += bins[b].count;
				left_count[b] = n;
				left_area_count[b] = bounds.surface_area() * static_cast<double>(n);
			}
			bounds = AABB();
			n = 0;
			for (int b = kBinCount - 1; b > 0; --b)
			{
				if (bins[b].count) bounds = AABB(bounds, bins[b].bounds);
				n += bins[b].count;
				if (left_count[b - 1] == 0 || n == 0) continue;
				// Unit traversal / primitive costs, compared with the leaf cost N.
				const double cost = 1.0 + (left_area_count[b - 1] + bounds.surface_area() * static_cast<double>(n)) / parent_area;
				if (cost < best_cost)
				{
					best_cost = cost;
					best_axis = axis;
					best_bin = b - 1;
				}
			}
		}

		if (count <= kMaxLeafSize && best_cost >= static_cast<double>(count))
		{
			make_leaf(primitives, start, end);
			return;
		}

		size_t middle = start + count / 2;
		if (best_axis >= 0)
		{
			split_axis = best_axis;
			const double extent = centroid_max[best_axis] - centroid_min[best_axis];
			const auto split = std::stable_partition(primitives.begin() + start, primitives.begin() + end,
				[&](const BuildPrimitive& p) {
					return bin_index(p.centroid[best_axis], centroid_min[best_axis], extent) <= best_bin;
				});
			middle = static_cast<size_t>(split - primitives.begin());
		}
		// Coincident centroids / invalid costs: stable count split guarantees progress.
		if (middle == start || middle == end) middle = start + count / 2;
		left.reset(new BVH_Node());
		right.reset(new BVH_Node());
		left->build(primitives, start, middle, depth + 1);
		right->build(primitives, middle, end, depth + 1);
	}
};

