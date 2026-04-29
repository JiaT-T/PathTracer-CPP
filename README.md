# PathTracer-CPP

一个使用 C++20 编写的路径追踪学习项目，整体实现参考了 [Ray Tracing](https://github.com/RayTracing/raytracing.github.io) 系列教程，并在此基础上继续扩展了三角形网格、OBJ 加载、重要性采样、体积介质和实时预览窗口等功能。

仓库地址：[JiaT-T/PathTracer-CPP](https://github.com/JiaT-T/PathTracer-CPP)

## 当前进展

### 已实现

- 几何体：`Sphere`、`Triangle`、`Quad`、`Box`
- 空间结构：`AABB`、`BVH`
- 变换：`Translation`、`Rotate_Y`、`Scale`
- 材质：`Lambertian`、`Metal`、`Dielectric`、`Diffuse_Light`、`isotropic`
- 纹理：`Solid_Color`、`Checker_Texture`、`Image_Texture`、`Noise_Texture`
- 采样：`Cosine_PDF`、`Hittable_PDF`、`Mixture_PDF`
- 其他：运动模糊、景深、Gamma 校正、体积介质、PPM 实时预览

### PBR 阶段性结果

- 新增 `PBR_Material`
- 材质接口拆分为 `Scatter + Eval + PDF`
- 积分式改为 `Le + f * Li * cos / pdf`
- 新增 `GGX_PDF`，降低光滑金属高光的 fireflies
- OBJ 现在支持读取 UV，并能按面分配 MTL 漫反射材质
- 新增 PBR 校验场景：四个球分别对比 `roughness` 和 `metallic`

### 当前限制

- `PBR_Material` 仍是最小可用版本
- 目前只覆盖 `baseColor / roughness / metallic`
- `normal map`、IBL、VNDF 采样、完整纹理工作流还没有接入

## 场景入口

主入口位于 [Renderer.cpp](PathTracer-CPP/Renderer.cpp)。

当前通过 `main()` 里的 `switch` 选择场景：

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

## PBR Test 说明

`PBR_Test()` 目前使用一个最小验证场景：

- 一块中性地面
- 一个顶部面积光源
- 四个球共享相同 `baseColor`
- 左两球为非金属，右两球为金属
- 外侧球较光滑，内侧球较粗糙

这个场景的目的不是做最终效果图，而是快速检查：

- 金属与非金属的响应是否分离
- 粗糙度是否影响高光扩散
- 采样策略是否引入明显亮点噪声

## Benchmark

`PBR_Benchmark()` 复用与 `PBR_Test()` 相同的校验场景，分别以串行和并行模式运行同一组参数，用于记录多线程渲染收益。

测试环境：

- CPU：`Intel Core Ultra 9 275HX`
- Scene：`PBR validation`
- Resolution：`800x450`
- Samples per pixel：`400`
- Max depth：`20`

实测结果：

- Serial：`65.3296 s`
- Parallel：`29.9941 s`
- Speedup：`2.17808x`
- Time reduced：`54.0881%`

当前并行渲染基于 `std::execution::par` 对 tile 进行并发调度，目标是复用同一套渲染逻辑，在不拆分算法路径的前提下提高多核 CPU 利用率。

## 项目结构

```text
PathTracer/
├── docs/
│   └── images/
├── README.md
└── PathTracer-CPP/
    ├── Renderer.cpp
    ├── Camera.h
    ├── Material.h
    ├── Texture.h
    ├── ObjLoader.h
    ├── Triangle.h
    ├── PDF.h
    ├── Hittable.h
    ├── BVH.h
    ├── PPMPreviewWindow.h
    ├── images/
    ├── Model/
    └── PathTracer-CPP.vcxproj
```

## 构建与运行

### 环境要求

- Windows
- Visual Studio 2022
- MSVC
- C++20

### 构建方式

当前仓库提供可直接打开的 Visual Studio 工程：

- `PathTracer-CPP/PathTracer-CPP.vcxproj`
- `PathTracer-CPP/PathTracer-CPP.slnx`

建议步骤：

1. 使用 Visual Studio 打开 `PathTracer-CPP.slnx` 或 `PathTracer-CPP.vcxproj`
2. 选择 `x64`
3. `Release` 用于正式渲染，`Debug` 用于调试
4. 运行项目

### 输出说明

- 渲染结果默认写入 `PathTracer-CPP/image.ppm`
- 控制台输出剩余扫描线与总渲染时间
- 程序会弹出实时预览窗口

## 纹理与资源

图像纹理通过 `rtw_stb_image.h` 加载，通常可从以下位置找到：

- 当前工作目录
- `PathTracer-CPP/images/`
- 环境变量 `RTW_IMAGES` 指定的目录

OBJ 加载会优先在模型所在目录内查找对应的 `.mtl` 与贴图文件。

## 后续计划

- 接入 `baseColor / roughness / metallic` 贴图工作流
- 完成 `normal map` 与切线空间支持
- 完成基于 GGX 的更稳定 BSDF 采样
- 增加 IBL 与环境贴图支持
- 继续完善多光源与 MIS 策略
- 增加更多针对材质系统的测试场景
