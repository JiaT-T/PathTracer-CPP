# Architecture

Short map of the renderer for maintenance. All paths are relative to `PathTracer-CPP/`.

## Data flow

```
Renderer.cpp (CLI)
  └─ Scenes.cpp: scene_registry() → SceneDesc { world, lights, use_lights, Camera }
       └─ Camera::Render / RenderProgressive
            ├─ LightSampler::build(lights, environment)          LightSampler.h
            ├─ PathIntegrator(world, light_sampler, env, ...)     Integrator.h
            └─ render loop: tiles × pixels × samples              Camera.h, Parallel.h
                 rng::begin_pixel_sample(seed, pixel, sample)   Sampler.h
                 ray = get_ray(i, j, s)
                 L = integrator.Li(ray, &PathSample)
                 accumulation += L;  AOVBuffers::accumulate()   RenderAOV.h
            → framebuffer (linear) → AtrousDenoiser              PostProcess.h
            → PFM (linear) / PPM (tone-mapped) / AOV files      ImageIO.h
```

## Components

| Component | File(s) | Responsibility |
|---|---|---|
| CLI | `Renderer.cpp` | argument parsing; render / `--test` / `--regress` / `--bench` / `--denoise-eval` / `--image-stats` |
| Scenes | `Scenes.h/.cpp` | scene builders return `SceneDesc`; they never render. Small `[test]` scenes for regression |
| Camera | `Camera.h` | camera model (pinhole, defocus disk, time), stratified + permuted sub-pixel samples, render loop, outputs |
| RNG | `Sampler.h`, `My_Common.h` | thread-local PCG32 re-seeded per (seed, pixel, sample) → results independent of thread scheduling |
| Parallelism | `Parallel.h` | `parallel_for(items, threads, fn)`: atomic work counter over 8×8 tiles, explicit thread count |
| Integrator | `Integrator.h` | iterative path tracer: NEE + BSDF sampling with multi-sample power heuristic, Russian roulette, `PathSample` recording |
| Light sampling | `LightSampler.h` | per-shading-point selection probabilities over geometry lights + environment (radiance × solid angle), mixture pdf |
| Materials | `Material.h` | `GetBSDF(ray, hit, BSDF&)`, `emitted()`, `Albedo()`; texture lookups happen here, once per hit |
| BSDF | `BSDF.h` | value type on the stack: Lambert, metallic-roughness PBR, isotropic phase, delta (mirror / glass). `sample / eval / pdf / cosine` |
| Microfacet | `Microfacet.h` | shared GGX math: D, Λ, G1, height-correlated G2, VNDF sampling / pdf, Schlick, tabulated specular albedo |
| PDFs | `PDF.h` | value types: `Cosine_PDF`, `Sphere_PDF`, `GGX_PDF` (used inside `BSDF`, tested directly) |
| Geometry | `Hittable.h`, `Sphere.h`, `Quad.h`, `Triangle.h`, `Constant_Medium.h` | intersection; `HitRecord` (non-owning `const Material*`, geometric + shading normal, tangent frame); light sampling (`random`, `pdf_value`, `light_shape_info`) for Sphere / Quad / Triangle |
| Acceleration | `BVH.h`, `AABB.h`, `Hittable_List.h` | recursive binary BVH, 16-bin SAH on all three centroid axes, explicit leaves, direction-ordered traversal |
| Environment | `Environment.h` | `LatLong_Environment` (2D CDF importance sampling), `Constant_Environment` (furnace tests) |
| Textures | `Texture.h`, `rtw_stb_image.h` | sRGB / linear decoding (single decode, fix F1), nearest-neighbour lookup |
| AOVs | `RenderAOV.h` | per-pixel accumulation of AOVs, luminance moments (variance), denoiser guides; visualization |
| Denoiser | `PostProcess.h` | variance-guided A-Trous with conservative albedo demodulation; `Settings::legacy()` = previous filter |
| Statistics | `Stats.h/.cpp` | per-thread ray / traversal counters, global allocation counter |
| Tests | `Tests.cpp` | numerical unit tests, regression against `tests/reference/*.pfm`, denoiser evaluation |
| Benchmark | `Benchmark.cpp` | thread-scaling table, counters, environment description |

## Acceleration

The BVH caches primitive bounds/centroids during construction and compares the SAH split
cost with testing a leaf. Leaves normally contain at most four primitives; coincident
centroids use a stable count split, and a depth limit of 64 prevents pathological recursion
(the depth-limit leaf may be larger). Each primitive is tested once per visited leaf.
Children are visited according to the ray direction on the split axis; this is a near-side
heuristic, not a sort by exact box entry distance. Equal-distance hits use the original input
index, with the later primitive winning as in `Hittable_List`. Ray intervals keep their
original lower bound, including negative bounds for medium boundary queries.

SAH construction does not consume random numbers. Changing traversal can change which
random draws a medium consumes relative to the old tree, while results remain reproducible
across thread counts. The former one-object leaf tested its medium twice; explicit leaves
remove that duplicate sampling. Nodes and triangle objects are still separately allocated;
this change does not implement a flat BVH, a contiguous mesh layout, or an any-hit shadow API.

## Path integrator

For each camera ray (`PathIntegrator::Li`):

1. Intersect. Add emission (or environment on a miss) with MIS weight
   `w_B = p_B² / (p_B² + (N_L p_L)²)` if the ray was a BSDF sample from a non-delta vertex,
   otherwise weight 1 (camera ray, after a delta event, or no light strategy).
2. Stop at `max_depth` scattering events, or if the material absorbs (`GetBSDF` returns false).
3. Delta BSDF: multiply the throughput by the delta weight, continue.
4. Otherwise NEE: `N_L` (4 at the first vertex, 1 afterwards) samples from the light mixture,
   weight `w_L = (N_L p_L)² / ((N_L p_L)² + p_B²)`, one shadow ray each (closest hit → emission).
5. Sample the BSDF, `β *= f cos / pdf`; this ray is both the continuation and the BSDF strategy of
   the MIS in step 1 of the next iteration.
6. Russian roulette from bounce 3: survive with `q = min(0.95, max(β))`, `β /= q`.

`PathSample` receives first-hit data (albedo, normals, depth, roughness, metallic), the radiance
split into emission / direct / indirect, first-vertex pdfs and MIS weight, the path length and the
denoiser guide (first non-delta vertex).

## Invariants protected by tests

- BVH intersections agree with a linear list for fixed rays, moving geometry, clipped and
  negative intervals; empty/degenerate inputs and equal-distance material selection are
  defined. Single-medium leaves preserve direct-query RNG state and Beer-Lambert scattering
  probability. A BVH-wrapped volume render is bit-identical across thread counts.
- Every sampling density integrates to 1 (or to 1 − P(invalid) for VNDF), and its samples follow it
  (χ² tests): cosine, sphere, GGX VNDF, full PBR mixture, environment, area / sphere lights,
  light-selection mixture.
- White furnace: Lambert = 1; white dielectric PBR = 1 ± 1%; white metal matches exact
  height-correlated Smith single scattering; phase function albedo exact.
- Reciprocity of the PBR BRDF; sampled pdf equals the separately evaluated pdf.
- Rendering: bit-identical results for 1 / 3 / all threads; RR on/off means agree; zero heap
  allocations per camera path; emission + direct + indirect = beauty; normal maps perturb the
  shading normal on triangles and spheres; denoiser reduces relMSE and preserves the mean.
- Regression scenes (`--regress`): analytic furnaces (mean = 1) and relMSE / mean against
  1024-spp references.
