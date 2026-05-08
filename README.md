# PathTracer-CPP

`PathTracer-CPP` is a C++20 CPU path tracer built for learning and extending physically based rendering techniques.  
The project started from the [Ray Tracing](https://github.com/RayTracing/raytracing.github.io) book series and has since been expanded with triangle meshes, OBJ/MTL loading, texture-driven materials, multithreaded tiled rendering, and a staged PBR pipeline.

Repository: [JiaT-T/PathTracer-CPP](https://github.com/JiaT-T/PathTracer-CPP)

## Current Status

### Implemented Core Features

- Geometry: `Sphere`, `Triangle`, `Quad`, `Box`
- Acceleration: `AABB`, `BVH`
- Transforms: `Translation`, `Rotate_Y`, `Scale`
- Legacy materials: `Lambertian`, `Metal`, `Dielectric`, `Diffuse_Light`, `isotropic`
- Textures: `Solid_Color`, `Checker_Texture`, `Image_Texture`, `Noise_Texture`
- Sampling infrastructure: `Cosine_PDF`, `Hittable_PDF`, `Mixture_PDF`
- Camera effects: depth of field, motion blur
- Volumetric rendering: constant medium
- Real-time preview window for PPM output
- Tiled multithreaded rendering based on `std::execution::par`

### PBR Phase 2

The renderer now has a usable metallic-roughness PBR pipeline:

- `PBR_Material` with `Scatter + Eval + PDF` separation
- Path throughput updated to the standard `Li * f * cos / pdf` form
- GGX microfacet BRDF with Schlick Fresnel and Smith masking-shadowing
- Texture-driven `baseColor / roughness / metallic / normal`
- Tangent-space normal mapping with TBN construction on triangle meshes
- `OpenGL / DirectX` normal map convention handling
- Input color-space separation:
  - `baseColor`: `sRGB -> linear`
  - `roughness / metallic / normal`: linear data
- Output display transform:
  - Reinhard tone mapping
  - `linear -> sRGB` encoding
- GGX VNDF sampling
- Light / BSDF MIS with power heuristic
- OBJ/MTL auto-material mapping into `PBR_Material`

### Current Limits

The renderer is now past the “PBR skeleton” stage, but it is still not a full production pipeline. Current gaps include:

- No IBL / HDRI environment lighting yet
- No environment importance sampling yet
- No clearcoat / anisotropy / subsurface / sheen layers
- No adaptive light-vs-BSDF strategy selection yet
- No EXR/HDR output path yet

## Scene Entry Points

Scene selection is controlled in [`D:\Computer Graphics\PathTracer\PathTracer-CPP\Renderer.cpp`](D:/Computer%20Graphics/PathTracer/PathTracer-CPP/Renderer.cpp) through the `switch` in `main()`.

Available entries:

- `1`: Bouncing Spheres
- `2`: Checker Spheres
- `3`: Earth
- `4`: Perlin Spheres
- `5`: Quads
- `6`: Lights Test
- `7`: Cornell Box
- `8`: Cornell Smoke
- `9`: Chapter Two Final Scene
- `10`: Triangle Test
- `11`: ObjTest
- `12`: Teapot
- `13`: Sponza
- `14`: PBR Test
- `15`: PBR Benchmark
- `16`: PBR Normal Map Test
- `17`: Obj PBR Test

## PBR Validation Scenes

### `PBR_Test()`

Minimal PBR validation scene used to inspect metallic-roughness behavior under controlled lighting.

Purpose:

- verify GGX response under area lights
- verify roughness / metallic behavior
- check VNDF + MIS stability after material-side sampling changes

### `PBR_Normal_Map_Test()`

Tangent-space normal mapping validation scene used to compare:

- PBR material with normal map
- PBR material without normal map

Purpose:

- verify TBN correctness
- verify normal map convention handling
- confirm that `Eval()`, `PDF()`, and camera-side `cos_theta` use a consistent shading normal

### `Obj_PBR_Test()`

Automatic asset-loading validation scene.

Purpose:

- verify `OBJ -> MTL -> Texture -> PBR_Material`
- verify auto-mapping of:
  - `map_Kd`
  - `norm`
  - `map_Pr`
  - `map_Pm`
- verify color-space handling and normal mapping on real loaded assets

## Benchmark

`PBR_Benchmark()` reuses the PBR validation scene and compares serial and parallel tiled rendering.

Benchmark setup:

- CPU: `Intel Core Ultra 9 275HX`
- Scene: `PBR validation`
- Resolution: `800x450`
- Samples per pixel: `400`
- Max depth: `20`

Measured result:

- Serial: `65.3296 s`
- Parallel: `29.9941 s`
- Speedup: `2.17808x`
- Time reduced: `54.0881%`

## Project Structure

```text
PathTracer/
├─ README.md
├─ docs/
└─ PathTracer-CPP/
   ├─ Renderer.cpp
   ├─ Camera.h
   ├─ Material.h
   ├─ PDF.h
   ├─ Texture.h
   ├─ ObjLoader.h
   ├─ Triangle.h
   ├─ Hittable.h
   ├─ BVH.h
   ├─ PPMPreviewWindow.h
   ├─ images/
   ├─ Model/
   └─ PathTracer-CPP.vcxproj
```

## Build and Run

### Requirements

- Windows
- Visual Studio 2022
- MSVC
- C++20

### Build

Open one of the project files:

- `PathTracer-CPP/PathTracer-CPP.vcxproj`
- `PathTracer-CPP/PathTracer-CPP.slnx`

Recommended configuration:

1. `x64`
2. `Release` for final rendering
3. `Debug` for feature debugging

### Output

- Render result is written to the file specified by `Camera::output_filename`
- Console shows progress and total render time
- A preview window is opened during rendering

## Asset and Texture Notes

Image textures are loaded through `rtw_stb_image.h`. Search order typically includes:

- current working directory
- `PathTracer-CPP/images/`
- the directory pointed to by `RTW_IMAGES`

OBJ loading resolves `.mtl` and referenced textures relative to the OBJ directory first.

## Next Steps

The next planned phase is environment lighting:

1. HDRI / lat-long environment texture support
2. Miss-radiance environment sampling in `Camera`
3. IBL validation scene
4. Environment importance sampling

After that:

- adaptive light-vs-BSDF strategy selection
- more advanced material lobes
- higher-quality output formats
