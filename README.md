# PathTracer-CPP

`PathTracer-CPP` 是一个使用 `C++20` 编写的 CPU 路径追踪器学习项目。  
项目最初参考了 [Ray Tracing](https://github.com/RayTracing/raytracing.github.io) 系列教程，并在此基础上逐步扩展了三角形网格、OBJ/MTL 加载、纹理驱动材质、多线程分块渲染以及分阶段推进的 PBR 流程。

仓库地址：[JiaT-T/PathTracer-CPP](https://github.com/JiaT-T/PathTracer-CPP)

## 当前进展

### 已实现的核心能力

- 几何体：`Sphere`、`Triangle`、`Quad`、`Box`
- 加速结构：`AABB`、`BVH`
- 变换：`Translation`、`Rotate_Y`、`Scale`
- 传统材质：`Lambertian`、`Metal`、`Dielectric`、`Diffuse_Light`、`isotropic`
- 纹理：`Solid_Color`、`Checker_Texture`、`Image_Texture`、`Noise_Texture`
- 采样基础设施：`Cosine_PDF`、`Hittable_PDF`、`Mixture_PDF`
- 相机效果：景深、运动模糊
- 体积渲染：常量介质
- 实时 PPM 预览窗口
- 基于 `std::execution::par` 的 tile 分块并行渲染

### PBR 第二阶段已完成内容

当前渲染器已经具备一条可用的 metallic-roughness PBR 管线：

- 新增 `PBR_Material`
- 材质接口拆分为 `Scatter + Eval + PDF`
- 路径积分统一为 `Li * f * cos / pdf`
- GGX 微表面镜面项，包含 Schlick Fresnel 和 Smith 遮蔽项
- 支持 `baseColor / roughness / metallic / normal` 贴图驱动
- 三角形切线空间 `TBN` 构建与 tangent-space normal map
- 支持 `OpenGL / DirectX` 法线贴图约定切换
- 输入颜色空间区分：
  - `baseColor`：`sRGB -> linear`
  - `roughness / metallic / normal`：线性数据
- 输出显示链路：
  - Reinhard tone mapping
  - `linear -> sRGB` 编码
- GGX VNDF 采样
- 基于 power heuristic 的 light / BSDF MIS
- OBJ/MTL 自动映射到 `PBR_Material`

### 当前限制

项目已经不是“PBR 雏形”，但仍然不是完整生产级流程。当前还缺少：

- IBL / HDRI 环境光照
- 环境贴图 importance sampling
- clearcoat、anisotropy、subsurface、sheen 等更复杂材质层
- 自适应的 light / BSDF strategy 选择
- EXR / HDR 输出链路

## 场景入口

场景入口位于 [D:\Computer Graphics\PathTracer\PathTracer-CPP\Renderer.cpp](D:/Computer%20Graphics/PathTracer/PathTracer-CPP/Renderer.cpp)，通过 `main()` 中的 `switch` 选择：

- `1`：Bouncing Spheres
- `2`：Checker Spheres
- `3`：Earth
- `4`：Perlin Spheres
- `5`：Quads
- `6`：Lights Test
- `7`：Cornell Box
- `8`：Cornell Smoke
- `9`：Chapter Two Final Scene
- `10`：Triangle Test
- `11`：ObjTest
- `12`：Teapot
- `13`：Sponza
- `14`：PBR Test
- `15`：PBR Benchmark
- `16`：PBR Normal Map Test
- `17`：Obj PBR Test

## PBR 验证场景

### `PBR_Test()`

最小 PBR 校验场景，用于检查 metallic-roughness 工作流在受控光照下的行为。

主要目的：

- 验证 GGX 在面积光下的响应
- 验证 roughness / metallic 对材质表现的影响
- 检查 VNDF + MIS 接入后的稳定性

### `PBR_Normal_Map_Test()`

法线贴图校验场景，用于对比：

- 带 normal map 的 PBR 材质
- 不带 normal map 的 PBR 材质

主要目的：

- 验证 TBN 构建是否正确
- 验证 OpenGL / DirectX 法线贴图约定切换
- 验证 `Eval()`、`PDF()` 和 `Camera` 中 `cos_theta` 使用同一条着色法线

### `Obj_PBR_Test()`

自动资产加载校验场景。

主要目的：

- 验证 `OBJ -> MTL -> Texture -> PBR_Material`
- 验证下列字段的自动映射：
  - `map_Kd`
  - `norm`
  - `map_Pr`
  - `map_Pm`
- 验证真实资产上的颜色空间与法线贴图接入链路

## Benchmark

`PBR_Benchmark()` 复用 PBR 校验场景，对串行和并行 tile 渲染做对比。

测试环境：

- CPU：`Intel Core Ultra 9 275HX`
- 场景：`PBR validation`
- 分辨率：`800x450`
- 每像素采样：`400`
- 最大深度：`20`

实测结果：

- Serial：`65.3296 s`
- Parallel：`29.9941 s`
- Speedup：`2.17808x`
- Time reduced：`54.0881%`

## 项目结构

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

## 构建与运行

### 环境要求

- Windows
- Visual Studio 2022
- MSVC
- C++20

### 构建方式

可直接打开以下工程文件：

- `PathTracer-CPP/PathTracer-CPP.vcxproj`
- `PathTracer-CPP/PathTracer-CPP.slnx`

推荐配置：

1. 使用 `x64`
2. `Release` 用于正式渲染
3. `Debug` 用于调试和功能验证

### 输出说明

- 渲染结果会写入 `Camera::output_filename` 指定的文件
- 控制台会输出进度和总渲染时间
- 渲染过程中会弹出实时预览窗口

## 资源与纹理说明

图像纹理通过 `rtw_stb_image.h` 加载，常见搜索路径包括：

- 当前工作目录
- `PathTracer-CPP/images/`
- 环境变量 `RTW_IMAGES` 指定目录

OBJ 加载时会优先在模型目录内查找 `.mtl` 与关联贴图。

## 后续计划

下一阶段计划聚焦环境光照：

1. HDRI / 经纬度环境贴图支持
2. `Camera` miss 分支环境辐射返回
3. `PBR_IBL_Test()` 场景
4. 环境贴图 importance sampling

之后再继续推进：

- 自适应 light / BSDF 采样策略
- 更复杂的材质层
- 更高质量的输出格式
