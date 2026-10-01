#pragma once
#include <functional>
#include <string>
#include <vector>

#include "Camera.h"
#include "Hittable_List.h"

// A fully described scene: geometry, explicit light list, and a configured camera.
// Scene builders only construct; rendering is done by the caller (CLI, tests, benchmark).
struct SceneDesc
{
	std::string name;
	Hittable_List world;
	Hittable_List lights;
	// true: next-event estimation towards `lights` (+ environment) with MIS.
	// false: pure BSDF path tracing (emitters/environment only found by chance).
	bool use_lights = false;
	Camera cam;
};

struct SceneEntry
{
	int id = 0;               // legacy switch number in main()
	std::string name;
	std::string description;
	std::function<SceneDesc()> build;
	bool test_scene = false;  // small scenes used by regression tests
};

const std::vector<SceneEntry>& scene_registry();
const SceneEntry* find_scene(const std::string& id_or_name);
