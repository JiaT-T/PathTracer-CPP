# PathTracer-CPP

一个使用 C++20 编写的路径追踪学习项目，整体实现参考了 [Ray Tracing](https://github.com/RayTracing/raytracing.github.io) 系列教程，并在此基础上继续扩展了三角形网格、OBJ 加载、PDF 重要性采样、体积介质和实时预览窗口等功能。

仓库地址：[JiaT-T/PathTracer-CPP](https://github.com/JiaT-T/PathTracer-CPP)

## 渲染效果

### Cornell Box

![Cornell Box](docs/images/cornell-box.png)

### Bouncing Spheres

![Bouncing Spheres](docs/images/bouncing-spheres.png)

## 更新记录

### 2026-04-24

- 新增 `PPMPreviewWindow`，支持渲染时实时窗口预览
- 预览窗口顶部显示渲染时间与分辨率
- 支持鼠标滚轮缩放、左键拖拽平移、右键双击重置视图
- 优化预览刷新逻辑，减少闪白和亮度跳变
- 修复三角形命中时法线处理问题，统一使用安全的 `shading_normal`
- 修复 `Triangle_Test()` 中发光三角形未加入 `world` 的问题
- 新增 `Teapot` 场景入口，并保留 `ObjTest` 网格测试场景

### 2026-04-17

- 合入路径追踪主线更新
- 完成基于 PDF 的重要性采样与直接光照采样框架
- 增加 `Mixture_PDF`、`Hittable_PDF`、余弦加权采样和面光源采样相关支持

### 2026-04-13

- 整理 README
- 明确项目结构、构建方式和场景入口说明

## 已实现功能

### 1. 几何体与空间结构

- 球体与运动球体 `Sphere`
- 三角形 `Triangle`
- 四边形 `Quad`
- 由六个四边形构成的 `Box`
- 包围盒 `AABB`
- BVH 加速结构 `BVH`
- 平移实例 `Translation`
- 绕 Y 轴旋转实例 `Rotate_Y`
- 恒定体积介质 `Constant_Medium`
- OBJ 网格加载 `ObjLoader`

### 2. 材质系统

- 漫反射 `Lambertian`
- 金属 `Metal`
- 介质 / 玻璃 `Dielectric`
- 发光材质 `Diffuse_Light`
- 各向同性介质材质 `isotropic`

### 3. 纹理系统

- 纯色纹理 `Solid_Color`
- 棋盘纹理 `Checker_Texture`
- 图像纹理 `Image_Texture`
- Perlin Noise 纹理
- 基于 Turbulence 的大理石纹理

### 4. 相机与渲染能力

- PPM 图像输出
- 渲染完成后自动打开窗口预览
- 渲染过程中实时刷新预览
- 顶部信息栏显示渲染时间和分辨率
- 预览窗口缩放 / 拖拽 / 视图重置
- 多重采样抗锯齿
- 分层采样 `Stratified Sampling`
- Gamma 校正
- 景深
- 运动模糊
- 背景颜色控制
- 直接光照采样
- 基于 PDF 的重要性采样
- 混合 PDF `Mixture_PDF`

### 5. 采样与数学基础

- ONB 正交基变换
- 球面 UV 映射
- 余弦加权半球采样
- 面光源采样
- 球体立体角采样
- Hittable PDF
- Monte Carlo 积分与 PDF 分离式结构

## 场景入口

主入口位于 [PathTracer-CPP/Renderer.cpp](PathTracer-CPP/Renderer.cpp)。
当前通过 `main()` 中的 `switch` 选择场景：

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

## 项目结构

```text
PathTracer/
├─ docs/
│  └─ images/
│     ├─ bouncing-spheres.png
│     └─ cornell-box.png
├─ README.md
└─ PathTracer-CPP/
   ├─ Renderer.cpp
   ├─ Camera.h
   ├─ PPMPreviewWindow.h
   ├─ Hittable.h
   ├─ Hittable_List.h
   ├─ Sphere.h
   ├─ Triangle.h
   ├─ Quad.h
   ├─ Constant_Medium.h
   ├─ Material.h
   ├─ Texture.h
   ├─ PDF.h
   ├─ AABB.h
   ├─ BVH.h
   ├─ ONB.h
   ├─ Vector3.h
   ├─ Ray.h
   ├─ Color.h
   ├─ My_Common.h
   ├─ Timer.h
   ├─ ObjLoader.h
   ├─ external/
   ├─ images/
   ├─ Model/
   └─ PathTracer-CPP.vcxproj
```

## 构建与运行

### 环境要求

- Windows
- Visual Studio 2022
- MSVC 工具链
- C++20

### 推荐构建方式

当前仓库提供可直接打开的 Visual Studio 工程：

- `PathTracer-CPP/PathTracer-CPP.vcxproj`
- `PathTracer-CPP/PathTracer-CPP.slnx`

建议步骤：

1. 使用 Visual Studio 打开 `PathTracer-CPP.slnx` 或 `PathTracer-CPP.vcxproj`
2. 选择 `x64` 平台
3. 使用 `Release` 配置进行正式渲染，`Debug` 配置更适合调试
4. 运行项目

### 运行输出

- 渲染结果默认写入 `PathTracer-CPP/image.ppm`
- 控制台会输出剩余扫描线和总渲染时间
- 程序会自动弹出实时预览窗口
- 预览窗口顶部显示渲染时间和分辨率

### 图像纹理说明

项目中的图像纹理通过 `rtw_stb_image.h` 加载，通常可通过以下方式找到纹理文件：

- 当前工作目录
- `images/` 目录
- 环境变量 `RTW_IMAGES` 指定的纹理目录

## 当前待办

- 引入 CPU 多线程渲染
- 为预览窗口增加暂停、保存快照或状态栏信息
- 继续扩展更完整的 MIS / 多光源采样策略
- 增加更多网格场景与材质测试案例
