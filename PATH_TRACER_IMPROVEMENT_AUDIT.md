# PathTracer-CPP 技术审查报告

> 审查基准：`main` @ `6208633`
> 范围：`PathTracer-CPP/` 下全部源码（27 个头文件、4 个 `.cpp`）、`README.md`、工程文件、资产目录
> 视角：离线渲染 / Rendering Engineer 面试官 / 做过 Path Tracer 的工程师
> 结论全部以源码为准。标了数字的结论来自审查期间写的临时数值程序（跑完已删除，方法见 §11，可以直接改成正式测试）。

---

## 0. 结论先行

1. **积分器骨架基本正确，超出一般学习型 Path Tracer**：NEE + BSDF 双策略、带样本数的 power heuristic、delta 路径处理、emission 抑制（没有重复计数）、立体角 PDF、环境贴图 2D CDF（验证 `∫pdf dω = 1.00000`），以及 VNDF 的采样/PDF 一致性（验证 `E[1/pdf] = 2π`），数学上都是自洽的。
2. **真正的错误集中在"积分器 ↔ 材质 ↔ 纹理 ↔ 场景"的接口处**。其中两个 Critical 已在本次审查中修复：
   - LDR 纹理被 `stbi_loadf` 隐式做了一次 gamma 2.2 解码：sRGB 被解码两次，法线贴图平均**倾斜 38.7°**。
   - 体积相位函数的 PDF 错了 4 倍，还额外乘了一个任意法线的 cos，体积能量只有正确值的约 **1/16**，并且只朝 +X 半球散射。
3. **还没修的最大数学问题是 Smith G**：用的是 UE4 给解析光源设计的 Karis 重映射 `k=(r+1)²/8`。白炉测试里 roughness 0.05 的白金属在 `cosθv=0.1` 时反照率只有 **0.20**（精确 Smith 应为 **1.00**），所有 PBR 球的边缘都会明显偏暗。
4. **性能瓶颈不在 BVH 或 virtual 本身**，在热路径上：每次非 delta 反弹要做 4–6 次 `make_shared`，每次图元命中都有一次 `shared_ptr<Material>` 的原子引用计数。README 里 24 核（8P+16E）只跑出 **2.18×**，这是需要解释的问题，不适合拿来展示。
5. **工程上最大的缺口是正确性不可验证、结果不可复现**：RNG 种子来自 `random_device` 和线程 ID；只能输出 8-bit、tonemap 之后的 PPM；没有任何测试。本次发现的每个 bug，用一个 20 行的白炉测试或 PDF 归一化测试就能提前抓到。
6. **README 展示图里的法线贴图完全没有生效**：展示场景用的是 `Sphere` 图元，它不提供切线空间。
7. **只能投入 1–2 周时**，建议做的 3 件事见 §19。

---

## 1. 本次审查已直接修复的问题

只修明确的 bug、UB、崩溃和错误公式，没有做重构。修改后用 `v143` 工具集编译 Release x64 通过（工程文件本身写的是 `v145`，本机没有）。

| # | 问题 | 文件 / 位置 | 等级 | 修复 | 画面影响 |
|---|---|---|---|---|---|
| F1 | `stbi_loadf` 默认对 LDR 做 `pow(x, 2.2)`；之后 `Image_Texture` 又做一次 sRGB 解码，数据贴图也被 gamma 解码了 | `rtw_stb_image.h` `rtw_image::load` | Critical | 调用 `stbi_loadf` 前先 `stbi_ldr_to_hdr_gamma(1.0f)` | 见下方实测 |
| F2 | `Sphere_PDF::value` 返回 `1/π`，应为 `1/(4π)` | `PDF.h` | Critical | 改为 `1/(4π)` | 体积散射 ×4 |
| F3 | 体积散射也乘了 `max(dot(rec.n, ω), 0)`，而 `Constant_Medium` 的 `rec.n` 固定是 `(1,0,0)` | `Camera.h`（5 处 cos）、`Material.h` `isotropic::Scatter` | Critical | 新增 `Scattered_Record::apply_cosine`，相位函数设为 `false` | 修复前体积期望吞吐 = albedo·E[cos⁺]/4 = albedo/16，并且只接受 +X 半球的散射方向 |
| F4 | 积分器的 cos 用 `rec.n`，而 PBR 的 `Eval`/PDF 用的是法线贴图扰动后的 `n` | `Material.h` `Scattered_Record::cosine_normal`；`Camera.h` `scatter_cosine()` | High | 由材质写入它实际使用的法线 | 法线贴图此前对漫反射几乎无效；额外的 `cos(rec.n,l)/cos(n_s,l)` 比值没有上界，会产生 firefly |
| F5 | `GGX_PDF::generate` 把反射到地平线以下的无效样本替换成 `+N`，而 `value(+N)` 没有计入这部分概率质量 | `PDF.h` | High | 原样返回，由 `value()=0` 丢弃 | 实测无效样本比例：r=0.6 时 11%，r=1.0 正入射时 50%。修复前这些概率质量全部堆在 +N，导致增能 |
| F6 | `Sphere` / `Quad` / `Constant_Medium` 从不写 `geo_n` 和 `has_tangent_space` | `Hittable.h` `set_face_front`、`Constant_Medium.h` | High | 在 `set_face_front` 里写入 | 修复前 `geo_n` 停留在默认的 (0,1,0)，或残留 BVH 遍历中上一个三角形的值：所有球和墙在 guide buffer 里法线相同，denoiser 的法线边缘停止失效 |
| F7 | 场景中的单面面积光朝向反了（`cross(u,v)` 朝上或背向物体） | `Renderer.cpp` `BuildPBRValidationScene`、`PBR_Normal_Map_Test` | High（场景） | 交换 `u`/`v` | **Benchmark 场景和法线贴图测试场景此前实际上没有面积光照明**，只靠 0.02 / 0.16 的背景色。已用程序验证修复前 `front_face=0`、修复后为 1 |
| F8 | PBR `Eval` 的"几何侧"判断用了插值法线 `rec.n` | `Material.h` `PBR_Material::Eval` | Medium | 改用 `rec.geo_n`（`HitRecord` 注释里本来就是这个定义） | 平滑网格剪影处的黑边；也会拒绝穿到真实表面以下的方向 |
| F9 | `Scale` 用 `inv_scale` 变换切线（切线应随 M 变换，法线随 M⁻ᵀ） | `Hittable.h` | Medium | 改为 `scale` | 只影响非均匀缩放 |
| F10 | `PBR_Material::Albedo` 在 `base_tex` 为空时解引用空指针 | `Material.h` | Crash | 与 `sample_color` 一致，返回白色 | OBJ 材质有 PBR 贴图、但 `Kd=0` 且没有 `map_Kd` 时崩溃 |
| F11 | `to_color_bytes` 只过滤 NaN，Inf 经过 Reinhard 变成 NaN 后再转 `unsigned char` 是 UB | `Color.h` | Low | 改用 `isfinite` | — |
| F12 | 头文件里非 `inline` 的自由函数和静态成员定义（ODR） | `Interval.h`、`AABB.h` | Low | 加 `inline` | 修复前无法新增第二个包含这些头文件的 TU（例如测试） |
| F13 | `GGX_PDF` 构造函数用未 clamp 的参数（遮蔽了同名成员）计算 alpha | `PDF.h` | Low | 改用 `this->roughness` | 当前调用方已经 clamp 过，没有实际影响 |
| F14 | `defocus_angle < 0` 判断：角度为 0 时仍然采样光圈 | `Camera.h` `get_ray` | Low | 改为 `<= 0` | 省掉一次拒绝采样 |

**F1 实测**（同一张图，旧 = stb gamma 2.2 → 8bit → [sRGB 解码]，新 = 原始编码值 → [sRGB 解码]）：

| 贴图 | 旧均值 | 新均值 |
|---|---|---|
| `Metal1/..._Color.jpg`（sRGB） | (0.943, 0.926, 0.841) | (0.972, 0.964, 0.923) |
| `Metal1/..._NormalGL.jpg` 平均切线空间法线 | (-0.441, -0.444, 0.780)，倾斜 **38.7°** | (0.001, -0.002, 1.000)，倾斜 0.1° |
| `Metal_Gold/..._NormalGL.jpg` | 倾斜 38.8° | 0.1° |
| 中灰 sRGB 0.5（理论值） | 0.040 | 0.214 |
| 线性 roughness 0.5（理论值） | 0.218 | 0.5 |

> 注：`Metal049A_Roughness.jpg` 实测均值 0.034，也就是这张图本身接近全黑。`sample_scalar` 会把它 clamp 到 0.05，所以这颗球实际上是一面镜子。这张贴图是否正确值得确认。

**修复后需要重新生成的内容**：`docs/images/*.png` 全部是修复前的结果。变化最大的场景依次是 `Cornell_Smoke`（体积）、`PBR_Normal_Map_Test`（面积光和法线贴图）、`PBR_Benchmark`、`Earth`、`README_Showcase`。

---

# Current Architecture

## 2.1 模块地图

| 层 | 文件 | 职责 | 备注 |
|---|---|---|---|
| Math / RNG | `Vector3.h` `Ray.h` `Interval.h` `ONB.h` `My_Common.h` | double 精度向量、RNG | `thread_local mt19937`，种子不可控 |
| Geometry | `Sphere.h` `Quad.h`(+`Box`) `Triangle.h` `Constant_Medium.h` `Hittable.h`(`Translation`/`Rotate_Y`/`Scale`) | 求交；光源采样（`random`/`pdf_value`）挂在 Hittable 上 | RTIOW 风格 |
| Accel | `AABB.h` `BVH.h` `Hittable_List.h` | 二叉 BVH | 递归、`shared_ptr` 节点 |
| Material | `Material.h` | `Scatter` / `Eval` / `emitted` | `PDF()`、`ShadingNormal()`、`BSDFSamplingPreference()` 是死代码 |
| Sampling | `PDF.h` | Cosine / Sphere / GGX-VNDF / Hittable / Environment / Mixture | 每次反弹都在堆上分配 |
| Texture | `Texture.h` `rtw_stb_image.h` `Perlin_Noise.h` | 最近邻采样、UV clamp | 每张图保存 float + byte 两份 |
| Environment | `Environment.h` | 经纬度 HDRI + 2D CDF | 挂在 `Camera` 上 |
| Integrator + Renderer | `Camera.h`（约 980 行） | 相机、两套积分器、MIS、RR、渐进循环、tile 并行、预览、降噪调度、输出 | 一个"上帝类" |
| Post | `PostProcess.h` | A-Trous | 单线程 |
| Asset | `ObjLoader.h`（tinyobj） | OBJ/MTL → `Triangle` / `PBR_Material` | |
| App | `Renderer.cpp` | 19 个硬编码场景，`switch (19)` | 没有 CLI |
| UI | `PPMPreviewWindow.h` | Win32 预览线程 | |

## 2.2 真实调用链（`README_Showcase`，带 lights 的主路径）

```
main() → switch(19) → README_Showcase()
  world = Hittable_List( make_shared<BVH_Node>(world) )
  lights = Hittable_List{ area_light (Quad) }
  cam.SetEnvironment( LatLong_Environment(hdr) )          // 环境光属于 Camera，而不是 Scene
→ RenderAndPreview → Camera::RenderProgressive(world, lights, &preview)
→ render_progressive_impl(sample_shader, guide_shader)
    initialize()                                            // sqrt_spp = floor(sqrt(1000)) = 31 → 实际 961 spp
    for sample_index in [0, 961):                           // 每个 sample 一个全图 pass（961 次 barrier）
      std::for_each(std::execution::par, tiles(4×4))
        get_ray(i, j, s_i = idx % 31, s_j = idx / 31)       // 像素分层抖动 + 光圈 + 随机 time
        ray_color(r, max_depth, world, lights, allow_emission = true)      ← 递归
          world.Hit → Hittable_List::Hit → BVH_Node::Hit（递归 virtual）→ Sphere/Quad/Triangle/Medium::Hit
          miss → miss_radiance: env->radiance（双线性）| background
          Le = allow_emission ? mat->emitted() : 0
          mat->Scatter → Scattered_Record{ attenuation, p_pdf（堆上 PDF 树）| skip_pdf_ray }
          [delta]  return Le + att · ray_color(skip_ray, allow_emission = true)
          [非delta]
            p_light = build_light_pdf(): Mixture( Hittable_PDF(lights), Environment_PDF, w_geo ∈ [0.05, 0.95] )
            N_L × sample_direct_light_once   (bounce0: N_L = 4, 其余 1)
                 ω ~ p_light；w = power(pdf_L·N_L, pdf_B·1)；f·cos·L(trace_direct_radiance)·w / pdf_L
            1   × sample_direct_bsdf_once    (第 2 条、独立的 BSDF 方向，只用于直接光 MIS)
            1   × sample_indirect_once       (第 3 条、独立的 BSDF 方向)
                 RR(bounce ≥ 3，p = max(本次 att·f·cos/pdf)) → ray_color(..., allow_emission = false)
          return Le + direct + indirect                     // throughput 隐含在递归的乘法中
      accumulation[i] += L;  framebuffer[i] = accumulation[i] / n
      sample 0 额外：guide_shader → trace_pixel_data(像素中心射线；无抖动/景深/运动模糊) → albedo / geo_n / t
      预览（≤30 Hz）：主线程逐像素 tonemap → BGRA
  AtrousDenoiser::Apply（2 pass，5×5 B3 核，step = 1, 2，单线程）
  write_framebuffer_to_file → to_color_bytes：逐通道 Reinhard → sRGB OETF → 8bit → ASCII P3 PPM
```

压缩成题目要求的形式：

```
Camera::get_ray
→ Hittable_List → BVH_Node(递归 virtual) → Primitive::Hit → HitRecord
→ Material::Scatter (堆上 PDF)
   ├─ Delta：直接递归（emission 开）
   └─ 非 Delta：build_light_pdf
        → N_L × Light sample (MIS) ┐
        → 1   × BSDF sample  (MIS) ┴→ trace_direct_radiance（最近交点的 emitted / 环境）
        → 第 3 条 BSDF 方向 → Russian Roulette → 递归（emission 关）
→ 返回值逐层乘 att·f·cos/pdf（隐式 throughput）
→ accumulation → A-Trous（2 pass）→ Reinhard → sRGB → 8-bit PPM
```

## 2.3 各环节定位

| 环节 | 位置 | 实现 | 评价 |
|---|---|---|---|
| Ray generation | `Camera::get_ray` | 像素分层抖动、圆盘景深、均匀 time | 分层按 pass 行优先遍历，中间结果有系统偏移（M7） |
| Intersection | `*::Hit` | 球：二次方程；Quad：平面 + αβ；三角形：Möller–Trumbore（det 用绝对阈值 1e-7）；体积：自由程采样 | 三角形在求交内部把 `p` 沿面法线偏移 0.001（H4） |
| BVH traversal | `BVH_Node::Hit` | 递归 virtual，固定先左后右，右子树用缩短后的 `t_max` | 没有近端优先，没有预计算 `inv_dir` |
| Material sampling | `Material::Scatter` | Lambert：cos；PBR：Mixture(cos, GGX-VNDF)，镜面权重 `0.5+0.5m`；Metal/Dielectric：delta | 每次调用 1–3 次 `make_shared` |
| BSDF evaluation | `Material::Eval` | 返回 f，不含 cos | PBR 的 G 用 Karis k（H1） |
| PDF | `s_rec.p_pdf->value` | 立体角测度 | `Material::PDF()` 从未被调用 |
| Direct lighting | `sample_direct_light_once` | `Hittable_PDF`（均匀选择光源）⊕ Env | 阴影射线求最近交点再取 `emitted` |
| MIS | 同上 + `sample_direct_bsdf_once` | power heuristic 考虑了样本数 | 数学一致 |
| Env sampling | `LatLong_Environment` | 2D CDF（lum·sinθ），二分查找 | 已验证正确 |
| Path throughput | 递归返回值 | 没有显式变量 | 没法基于 throughput 做 RR，也没法调试路径 |
| Russian Roulette | `sample_indirect_once` | bounce ≥ 3，p = max(本次权重)，clamp 到 [1e-4, 0.9999] | 无偏，但生存率偏高 |
| Emission | `allow_emission` 标志 | 相机射线和 delta 之后计入；非 delta 之后抑制，由 BSDF-direct 样本带 MIS 计入 | 正确，没有重复计数 |
| Texture sampling | `Image_Texture::value` | 最近邻、UV clamp、8-bit | 不支持 tiling UV |
| Normal mapping | `PBR_Material::sample_shading_normal` | 每面一个 T/B + Gram-Schmidt，强制右手系 | 球没有切线（H6） |
| Progressive | `render_progressive_impl` | 每 pass 1 spp，double 累加 | |
| Multithreading | `std::for_each(par)` | 4×4 tile | |
| Denoising | `AtrousDenoiser` | 见 §7 | |
| Tone mapping | `Color.h` | 逐通道 Reinhard | 没有曝光参数 |
| Output | `write_framebuffer_to_file` | 8-bit ASCII P3 PPM | 没有 HDR 输出 |

## 2.4 README 与代码不一致

1. **"normal map 驱动的微表面反射"**：`README_Showcase` 和 `PBR_IBL_Test` 的 PBR 物体都是 `Sphere`，`Sphere` 不设置切线空间，`sample_shading_normal` 直接返回 `rec.n`。**展示图里法线贴图不生效。**
2. **"sRGB → linear 输入颜色空间处理"**：修复前实际是 stb 的 gamma 2.2 再叠加一次 sRGB 解码（F1）。
3. **"外层 BSDF / light choose probability 仍然是启发式"**：当前积分器根本不在 BSDF 和 light 之间做选择，而是固定 4(1)+1 个样本，`BSDFSamplingPreference()` 是死代码。真正的启发式在 geo/env 混合权重和漫反射/镜面 lobe 选择概率上。
4. **"几何光 / 环境光自动能量估计"**：代码存在，但两者单位不一致，结果永远落在 clamp 下限 0.05（M1）。
5. **"Sponza 缺更稳健的 triangulation"**：tinyobj 默认已经做 fan 三角化。Sponza 的真实问题是：`map_bump`（高度图）被当成法线贴图、UV 被 clamp、`map_d` 被忽略，而且 `sponza.obj` 不在仓库里。
6. **构建**：README 写的是 VS2022，工程文件是 `PlatformToolset=v145`（VS 2026）；`CMakeLists.txt` 是空文件。
7. **"场景入口"链接**是本机绝对路径 `D:\Computer Graphics\...`。
8. `ObjTest`、`PBR_Test`、`Sponza` 引用的 `Model/dragon.obj`、`Model/sphere.obj`、`Model/sponza.obj` 不在仓库中。
9. **Benchmark**：2.18× 是在 24 核 CPU 上跑出来的（§5 P-2）；而且修复前这个场景的面积光朝上，并没有照到物体（F7）。
10. `sample_per_pixel = 1000` 实际只跑 961 spp，README 没有说明。

---

# Correctness Risks

> 等级代表对渲染结果的技术影响。[已修复] 的条目详见 §1，这里不再展开。

## Critical

- **C1 [已修复 F1]** LDR 纹理隐式 gamma。
- **C2 [已修复 F2+F3]** 体积相位函数 PDF 与 cos。

目前没有未修复的 Critical。

## High

### H1 Smith G 使用 Karis 重映射，与 VNDF 的 Smith 不一致【未修复】

- 位置：`Material.h` `PBR_Material::geometry_smith`：`k = (r+1)²/8`，Schlick-GGX 近似。
- 原理：`(r+1)²/8` 是 UE4 专门为**解析点光源**做的"hotness"重映射，并不是 GGX 的 Smith G。`GGX_PDF` 的 VNDF 采样和 PDF 用的是精确的 Smith G1（α = r²），两者不是同一个微表面模型。对完美的镜面金属，正确结果应满足 `f·cos/pdf_vndf = F·G1(l)`，也就是不含 G1(v) 的衰减。
- 实测（白色金属 base=1，F=1，200 万样本）：

| roughness | cosθv | 当前 Eval 反照率 | 精确 Smith（单次散射） |
|---|---|---|---|
| 0.05 | 1.0 | 1.000 | 1.000 |
| 0.05 | 0.5 | **0.772** | 1.000 |
| 0.05 | 0.1 | **0.199** | 1.000 |
| 0.30 | 0.1 | **0.221** | 0.876 |
| 0.60 | 0.5 | 0.554 | 0.777 |
| 1.00 | * | ≈ 精确值 | — |

- 影响：所有光滑到中等粗糙的 PBR 金属边缘明显变暗。低粗糙度金属本应接近完美镜面，现在在掠射角损失最多 80% 的能量。这一点在展示图的主球上最显眼。
- 修复：`G = G1(v)·G1(l)`，`G1(x) = 2/(1+√(1+α²tan²θ))`，α = r²；更好的是 height-correlated 形式 `G2 = 1/(1+Λ(v)+Λ(l))`。可以直接复用 `GGX_PDF::Smith_G` 的写法。约 10 行。

### H2 [已修复 F4] 余弦项与材质用的法线不一致

### H3 [已修复 F5] VNDF 无效样本被塞进 +N

### H4 三角形求交内部偏移 + 固定 epsilon：透射自相交，行为与尺度相关【未修复】

- 位置：`Triangle::Hit` 里的 `rec.p ± face_normal * 0.001`（朝入射一侧）；全局 `t_min = 0.001`；`Quad`/`Sphere` 不做偏移。
- 问题：
  1. 透射射线从入射一侧出发、朝平面另一侧前进，会在 `t ≈ 0.001/|cosθ| ≥ t_min` 处再次命中同一个三角形（`contains` 是闭区间）。**用三角网格建模的玻璃或体积边界会被错误反射或卡住。**
  2. 0.001 是绝对世界单位：Cornell 盒尺度（555）下没有问题，1 cm 尺度的模型会漏光或出现黑斑。
  3. 偏移后的 `rec.p` 不再在表面上。`Constant_Medium` 只用 `t`，暂时不受影响。
- 修复：求交保持 `p` 在表面上；在**生成新射线时**按出射方向所在的半球沿 `geo_n` 偏移（`offset_ray_origin(p, geo_n, dir)`），epsilon 与 `max(|p|)` 成比例（参考 Ray Tracing Gems ch.6 的整数 ULP 偏移）。

### H5 [已修复 F6] `geo_n` / `has_tangent_space` 未写

### H6 法线贴图 TBN 的覆盖面与手性【未修复】

- 问题：
  1. `Sphere` 不提供切线：**README 展示图的法线贴图不生效**。球的解析切线很简单：`T = normalize(∂p/∂φ)`，`B = N×T`。
  2. 切线按面计算、不在顶点上平滑：在平滑着色网格上，法线贴图的效果会在三角形边界出现接缝。
  3. `sample_shading_normal` 强制把 `B` 翻成 `cross(N,T)` 的方向：**UV 镜像的资产（左右对称模型很常见）法线的 Y 分量会反。** 应该在每个三角形上保存 handedness 符号 `w = sign(dot(cross(N,T), B))`，并用 `B = w·cross(N,T)` 重建。
  4. 法线贴图偏离几何法线后，没有任何能量保护（shadow terminator、黑斑）。当前的 `correct_shading_normal_to_direction` 只修正了视线方向。
- 修复顺序：①球的切线（让展示图生效）；②handedness；③按顶点累加切线（MikkTSpace 的简化版）；④可选：Chiang 2019 的 shadow terminator 项，或 Estevez 2019 的 microfacet-based normal mapping。

## Medium

### M1 光源选择权重：单位不一致 → 永远是 5% / 95%（无偏，但方差大）

- 位置：`Camera::build_light_pdf`
- `env_power = intensity × Σ(lum·sinθ)`，这是一个**像素求和**，和贴图分辨率成正比；`geo_power = Σ(L_lum × A)`，是面积光的通量密度。两者量纲不同。
- 实测 `README_Showcase`：`env_power = 928131`、`geo_power = 460`，得到 `w_geo = 0.0005`，被 clamp 到 **0.05**。而分辨率无关的 `∫L_env dω ≈ 8.7`；从场景中心看，面积光的 `L·A·|cos|/d² ≈ 9.9`。**理想分配接近 50/50，实际是 5/95。**
- 影响：面积光的直接光（软阴影、金属上的高光）方差被放大到数倍。
- 修复（约 15 行）：env 用 `total_weight × 2π²/(W·H) × intensity`；geo 按着色点估计 `Σ L_i·A_i·max(cosθ_light, ε)/d²`（或者先用全局常数，已经比现在好得多）。

### M2 不能被采样的"光源"会返回假方向（潜在偏差）

- `Hittable::random()` 默认返回 `(1,0,0)`，`pdf_value()` 默认返回 0。`Translation`、`Rotate_Y`、`Scale`、`BVH_Node`、`Triangle::sampling_power_estimate`、`Sphere::sampling_power_estimate` 都没有转发或实现。
- 把变换后的物体或网格放进 `lights`，NEE 会朝固定方向发射射线并得到 `pdf = 0`。当前代码会跳过这个样本，但在 Mixture 中这部分概率质量**被静默丢弃**（能量损失），而 BSDF 侧的 MIS 权重仍按完整的 light pdf 计算，结果有偏。
- 另外，空的 `lights` 列表和环境光混合时，`w_geo` 仍然是 ≥ 0.05。
- 修复：`lights` 为空或 `power = 0` 时从 Mixture 中剔除；不支持采样的类型在 `add` 时就报错。

### M3 PBR 能量守恒细节

- `kd = (1 − F(v·h))(1 − m)`：漫反射层用的是逐方向的微表面 Fresnel，并不对应一个能量守恒的分层模型。实测 m=0、r=0.05、cosθv=0.1 时白炉反照率 **1.06（增能）**。
- 没有多次散射补偿：粗糙金属（r=0.6，正入射 0.82）偏暗，属于单次散射的固有损失，可以用 Kulla-Conty 查找表修复（P2）。
- `cook_torrance_specular` 的分母 `4·n·v·n·l + 1e-4`：在极端掠射角会压暗约 20%（n·v = n·l = 0.01 时）。直接去掉 `+1e-4`（已经对 n·v 和 n·l 做了 clamp）。
- 注意：**metallic = 1 时漫反射确实完全消失**（`kd` 乘了 `(1−m)`，lobe 权重为 0），这一点是正确的。

### M4 Lobe 选择概率与能量不匹配（方差）

- `specular_weight = 0.5 + 0.5m`：对 m=0 的电介质，50% 样本分给了只携带约 4% 能量的镜面 lobe。
- 这不影响无偏性（Mixture 的 PDF 是一致的），但粗糙电介质的漫反射间接光方差显著偏大。
- 修复：`p_spec = lum(F_schlick(n·v, F0)) / (lum(F_schlick) + lum((1−F)(1−m)·base))`，再 clamp 到 [0.1, 0.9]。

### M5 OBJ 资产语义错误

- `map_bump` / `bump` 在 MTL 中是**高度图**，被当成切线空间法线贴图加载（`ObjLoader::get_or_create_material`）。Sponza 的所有 `*_bump.png` 都是灰度高度图，会得到完全错误的法线。
- UV 被 clamp 到 [0,1]：tiling 纹理（Sponza 的地面、墙面）会被拉成条纹。
- 忽略 `map_d`：Sponza 的植物、铁链没有镂空。
- 忽略 MTL 的标量 `Pr` / `Pm`：没有粗糙度贴图时一律按 roughness = 1.0 处理。

### M6 NaN / Inf 没有检测

- 单个 NaN 样本会永久污染 `accumulation`，输出时被 `to_color_bytes` 变成黑点，看起来像"莫名其妙的黑像素"。
- 常见来源：`Sphere::pdf_value`（原点在球内时 `sqrt(负数)`）、零面积三角形的 `normalize(0)`、PDF 下溢。
- 修复：累加前检查 `isfinite`，记录计数和第一个出错的像素坐标（调试用），并丢弃该样本。

### M7 渐进分层顺序：中间结果有偏

- `s_i = idx % sqrt_spp`、`s_j = idx / sqrt_spp`：前 31 个 pass 全部落在每个像素子格的最上一行，中间预览存在系统性的半像素偏移，直到最后一个 pass 才恢复正确。
- 修复：对 stratum 下标做一次按像素的随机排列（或直接换成低差异序列，见 P2-2）。

### M8 `Metal` 的 fuzz 不拒绝地平线以下的方向

- `reflected + fuzz·random_unit_vector()` 可能指向表面以下。RTIOW 原版会检查 `dot(scattered, n) > 0` 并吸收，这里删掉了这个检查，光线会穿进物体内部。

### M9 Sphere 作为光源的缺陷

- `sampling_power_estimate()` 返回 0：`Lights_Test` 这类用球光源的场景，在混合环境光时权重是错的。
- `pdf_value` 和 `random` 用的是 `center.at(0)`：移动的球光源采样位置不对。
- 原点在球内时出现 NaN（M6）。

### M10 `Cornell_Box` 把玻璃球放进了 `lights`

- 这是 RTIOW 第三本的做法，意图是"把采样引向玻璃"。但当前积分器会把 `lights` 当成**发光体**做 NEE：朝玻璃球发出的阴影射线命中玻璃，`emitted = 0`，于是一半的光源样本被浪费，BSDF 侧的 MIS 权重也被这部分无用的 light pdf 稀释。
- 修复：把玻璃球移出 `lights`。

## Low

- **L1** `Dielectric`：从光密介质射出时，Schlick 使用的是入射角的 cos，应改用透射角的 cos（Fresnel 反射率不对称）。
- **L2** `Triangle` 的 det 用绝对阈值 1e-7：边长约 1 mm 的三角形会被漏掉，应改成相对阈值或直接判断 `det == 0`。
- **L3** `BVH_Node`：空列表时是 UB；`span == 1` 的叶子 `left = right`，同一个图元会被测试两次。
- **L4** 环境贴图显示用 `u·(W−1)`，PDF 用 `u·W`，存在半个纹素的错位。只影响方差，不影响无偏性。
- **L5** 逐通道 Reinhard 会造成高亮区域的色相偏移，并且没有曝光参数。
- **L6** `Perlin` 和场景的随机布局都依赖 `random_device`，场景内容本身不可复现。
- **L7** 死代码：`Material::PDF` / `ShadingNormal` / `BSDFSamplingPreference`。`PBR_Material::PDF` 的注释写着"必须与 Scatter 保持一致"，实际上从未被调用。
- **L8** `cook_torrance_specular(float roughness)`：double → float（C4244）。

---

## 4. 噪声来源分析

只列当前项目**实际存在**的主要方差来源，按影响排序。

| # | 来源 | 场景 | 当前策略 | 是否有效 | 应对 |
|---|---|---|---|---|---|
| N1 | **面积光被欠采样** | README_Showcase、PBR_IBL_Test | geo/env 权重被 clamp 到 0.05 | ✘ 实际需要约 50% | 修复 M1（P0，约 15 行） |
| N2 | **每次反弹多一条射线** | 全部 | bounce0：4 light + 1 BSDF-direct + 1 indirect；之后 1+1+1 | ◐ 正确但浪费 | 用 continuation ray 兼做 BSDF 的 MIS 样本：命中发光体时加上 `w_B·Le` 再继续。深层每次反弹省 1/3 的射线，同样的时间可以多跑约 30–50% spp（P1-2） |
| N3 | **lobe 选择** | 所有 m≈0 的 PBR | 50% 样本给了约 4% 能量的镜面 lobe | ✘ | M4（P1-2 顺带完成） |
| N4 | **体积被玻璃壳包裹** | README 雾球 | 体积内的 NEE 永远被 Dielectric 外壳挡住 | ✘ 属于场景设计问题 | 如果想要的是"雾球"，就不要把 `fog_boundary` 加入 world（参考 `Cornell_Smoke`）。修复 F2/F3 之后能量才对 |
| N5 | 玻璃后方的焦散 / 看穿玻璃的漫反射 | Cornell_Box、Showcase | 只能靠 BSDF 路径碰巧命中光源 | 纯路径追踪的固有限制 | 本项目规模不建议上 BDPT/MLT；在场景设计上避开即可 |
| N6 | normal map firefly | 带法线贴图的 PBR | 修复前 `cos(rec.n)/cos(n_s)` 无界 | 已修复（F4） | 剩余的是 shadow terminator（H6-4） |
| N7 | RR 生存率偏高 | 深路径 | 基于本次反弹权重：暗路径也会高概率存活 | ◐ | 迭代积分器里改为基于 throughput（P1-2） |
| N8 | 采样序列 | 全部 | 像素维度分层，其余维度全是独立随机数 | ◐ | 低差异序列：Sobol + Owen 或 R2 + Cranley-Patterson（P2-2） |
| N9 | 低粗糙度 GGX 反射小光源 | Showcase 金属球 | MIS + VNDF | ✔ 已经有效 | 不需要改 |
| N10 | HDRI 太阳 | IBL 场景 | 2D CDF 重要性采样 | ✔ 已验证正确 | 不需要层级采样或 alias table |

**技术适配结论**：

- **Better Light Sampling**（修复 M1 + 按着色点的功率估计）：✔ 立即做，成本最低、收益最直接。
- **BRDF Importance Sampling**：已有 VNDF ✔；只需要修正 lobe 选择（M4）。
- **Multiple Light Sampling**：bounce0 的 4 次 NEE 可以保留，但应在 N2 完成之后重新评估它的收益（用 §11 的 RMSE-vs-时间曲线衡量）。
- **Next Event Estimation**：已有。
- **Hierarchical Environment Sampling / Alias Table**：✘ 2D CDF 已经是正确的重要性分布，alias table 只省下一次 O(log n) 的二分查找，不会改变方差。
- **Reservoir Sampling / ReSTIR**：✘ 场景只有 1–2 个面积光 + 1 个环境光，是离线渲染，没有需要复用样本的多光源问题。

---

# Performance Bottlenecks

每项按"当前实现 / 可能瓶颈 / 是否值得 / 如何验证"给出。★ 越多越值得做。

### P-1 热路径上的堆分配 ★★★

- **当前**：每次非 delta 反弹要做：`Scatter` 1–3 次 `make_shared`（PBR：Cosine + GGX + Mixture）；`build_light_pdf` 1–3 次（Hittable + Env + Mixture）。Lambert 地面 1 + 3。所有 `PDF` 都是 virtual 对象树。
- **瓶颈**：MSVC 的堆在 24 个线程下竞争明显；每次分配约 50–100 ns，外加 `shared_ptr` 控制块的原子操作；每条相机路径有几十次分配。
- **值得**：是。改成栈上的 `BSDF` 值类型（`struct { Frame; Color base; double rough, metal, p_spec; }` + `sample/eval/pdf` 成员函数），光源采样用 `LightSampler` 的普通函数。和 P1-2 的迭代积分器一起做，改动量才合理。
- **验证**：在 benchmark 构建里全局重载 `operator new`，用原子计数器统计"每条相机射线的分配次数"（目标：0）；对比修改前后的 wall time 和线程扩展曲线。

### P-2 `shared_ptr` 原子引用计数在多线程下的竞争 ★★★

- **当前**：`HitRecord::mat` 是 `shared_ptr<Material>`，每次图元命中都会拷贝一次（加一次原子增减）；`Hittable_List::Hit` 的 `rec = temp_rec` 又拷贝一次；同一个材质（例如地面）被所有线程同时增减引用计数，同一条 cache line 在核心之间来回迁移。
- **瓶颈**：这是 **2.18× / 24 核**的头号嫌疑。8P+16E 的理想加速比大约是 8 + 16×0.6 ≈ 17×。非渐进的 `Render` 路径中几乎没有串行段，Amdahl 定律解释不了这个数字。
- **值得**：是，而且改动非常小：`HitRecord` 改用 `const Material*`（场景持有所有权，渲染期间不会释放）。
- **验证**：先用 `start /affinity 0x1 / 0x3 / 0xF / 0xFF / 0xFFFFFF PathTracer-CPP.exe` 测出 1/2/4/8/24 核的扩展曲线；改成裸指针之后再测一次。VS Profiler 的 CPU Usage 或 VTune 的 Threading 分析可以直接看到 `_InterlockedIncrement` 热点。

### P-3 材质参数在每次反弹中被重复求值 ★★

- **当前**：一次 PBR 反弹（bounce0）会调用 1 次 `Scatter` + 6 次 `Eval` + 5 次 `PDF::value`。每次 `Eval` 都重新采样 base/roughness/metallic/normal 四张纹理并重建 TBN；bounce0 大约要做 25 次纹理 fetch。
- **值得**：是。与 P-1 一起做：`Scatter` 阶段一次性求出 BSDF 参数，之后的 eval/pdf 只做数学运算。
- **验证**：纹理 fetch 计数器；profiler 中 `Image_Texture::value` 所占的比例。

### P-4 每次反弹的射线数 ★★

- 见 N2。bounce0 有 6 条射线（4 条阴影 + 2 条 BSDF 最近交点），之后每次 3 条；标准的 NEE + MIS 结构是每次 2 条。
- **验证**：射线计数器（按 camera / shadow / continuation 分类），以及同等时间下的 RMSE。

### P-5 BVH 结构与遍历 ★★（网格场景）/ ★（当前展示场景）

- 见 §6。当前展示场景只有十几个图元，BVH 不是瓶颈；要渲染 Sponza 或 dragon 这类网格时它会成为主要瓶颈。

### P-6 三角形内存布局 ★★（网格场景）

- **当前**：每个 `Triangle` 是一个独立的堆对象，保存 3 个顶点、3 条法线、3 个 UV、face_normal、T、B、bbox、`shared_ptr<Material>`，约 400+ 字节。求交只需要 36 字节的顶点数据，却要把整个对象拉进缓存；每个三角形还要一次 virtual 调用和一次 `shared_ptr` 解引用。
- **值得**：做 BVH 扁平化（P1-4）时顺带改成 `Mesh { positions[], normals[], uvs[], indices[], material_ids[] }`，BVH 叶子只保存三角形下标。属性只在命中之后再取。
- **验证**：Sponza（约 26 万个三角形）的 MRays/s。

### P-7 阴影射线求最近交点 ★

- `trace_direct_radiance` 用的是完整的最近交点求交，再取 `emitted`。对"光源采样 + 可见性"来说，任意交点提前退出（any-hit）就够了，但当前需要最近交点才能判断命中的是否就是被采样的那个光源。
- **值得**：做迭代积分器时，改成"采样光源上的一个点 + 对 `t < d − ε` 的区间做 occlusion 测试"，可以同时拿到 any-hit 的收益。

### P-8 渐进渲染里的串行段 ★

- 预览的像素转换（最多 30 Hz）、最后一次降噪，都在主线程上单线程执行；而且 961 个 pass 各有一次 barrier。
- **值得**：低成本改进：每个 pass 渲染 4–16 spp；降噪按行并行（`for_each(par)`）。

### P-9 RNG ★（性能）/ ★★★（可复现性）

- `thread_local mt19937`：状态 2.5 KB，没有竞争，性能可以接受；但**同一个像素在两次运行中使用的随机序列不同**，渲染结果不可复现。
- **值得**：改成按 (pixel, sample, dimension) 派生种子的 PCG32 或基于哈希的计数器 RNG（P0-1）。

### P-10 Virtual dispatch ★（不建议单独优化）

- 每条射线有 `BVH 深度 × (Hit + AABB)` 次 virtual 调用，外加材质、PDF、纹理的 virtual 调用。开销真正大的是 virtual 背后的**指针追逐和 cache miss**，而不是间接跳转本身。BVH 扁平化（P1-4）加上栈上 BSDF（P-1）会消除绝大部分；材质和纹理层面的 virtual 可以保留。

### P-11 并行调度 ★（`std::execution::par` 已经够用）

- **tile size**：4×4 = 16 像素。1280×720 有 57600 个 tile，每个 tile 每个 pass 只算 16 个样本；粒度足够细，负载均衡没有问题。
- **false sharing**：一行 tile 的 4 个像素 × 24 B = 96 B，相邻 tile 共享 cache line；但每个像素每个 pass 只写一次，和路径计算相比可以忽略。
- **同步**：非渐进路径每个 tile 拿一次 `progress_mutex`（57600 次），可以忽略。
- **结论**：MSVC 的 `par` 底层已经是 Windows 线程池（带工作窃取）。custom thread pool、work stealing、persistent workers 都**不值得**做：扩展性问题出在 P-1/P-2 的竞争上，而不是调度上。

### P-12 AoS → SoA / SIMD ✘（目前不值得）

- 标量遍历配合 32 字节的紧凑 AoS BVH 节点，才是缓存最友好的布局。SoA 只有在 4-wide BVH + SIMD 求交时才有意义（见 §17）。
- `Vector3` 用 double：在 AVX2 上吞吐大约是 float 的一半，但换成 float 需要全局改动，同时要重新处理自相交 epsilon。等 P-1、P-2 解决之后，再用 profiler 判断是否值得。

---

## 6. BVH 专项

| 项 | 当前 |
|---|---|
| Build strategy | 自顶向下，递归 `make_shared<BVH_Node>` |
| Split axis | 父节点包围盒的最长轴 |
| Split method | **Object median**：按图元包围盒的 `min` 排序，从中间一分为二（不是按质心） |
| Sort | 每层 `std::sort`，建树复杂度 O(n log² n) |
| Leaf | 1–2 个图元，`span == 1` 时 `left = right` |
| Traversal | 递归 virtual；固定先左后右；右子树用缩短后的 `t_max` |
| AABB test | 每个轴计算一次 `1/dir`（每个节点 3 次除法）；有提前退出 |
| Memory | 每个节点一个堆对象 + 两个 `shared_ptr`；节点与图元散落在堆上 |
| `BVH_Node(Hittable_List list)` | 按值传入：拷贝整个 vector 以及其中所有 `shared_ptr`（原子操作） |

判断：**Median split（以 bbox.min 为键）**。它既不是 random split，也不是 centroid split，更不是 SAH。

**SAH 的真实收益在哪里**：

- 当前展示场景（十几个球、一块大地面、一个面积光）：**收益接近 0**。地面 quad 的包围盒覆盖了整个场景，不管用什么分割方法，大部分射线都要测试它。
- Chapter 2 最终场景（1000 个小球 + 400 个 box）：大约 1.2–1.5×。
- **大小差异悬殊的三角网格**（Sponza：大块墙面加上细小的装饰）：median split 会把大三角形和小三角形混在同一个节点里，导致节点包围盒严重重叠。这种情况下 SAH（binned，12–16 个 bin）通常能带来 **2–3×** 的遍历加速。
- 所以 SAH 的价值取决于项目是否打算渲染真实的网格资产。如果打算（推荐，见 §8），SAH 应该和扁平化一起做。

**建议顺序**（P1-4）：

1. 按质心分割、binned SAH（O(n log n)，12 个 bin），叶子最多 4 个图元。
2. 扁平化为 32 字节节点的数组（`bbox_min[3], bbox_max[3]` 用 float，`left_or_first, count`），左子节点紧跟父节点存放。
3. 迭代遍历：固定大小的栈（64），预计算 `inv_dir` 和 `dir_is_neg[3]`，**按射线方向先访问近端子节点**，节点入栈前用当前 `t_max` 剪枝。
4. 图元改为 mesh 索引（P-6）；`Sphere` / `Quad` 保留为"其他图元"数组，或直接三角化。

**验证**：先加上"每条射线的节点访问数 / 图元测试数"统计和 BVH 热力图（§12），再对比 Chapter 2 最终场景和 Sponza（需要补上资产）的 MRays/s 与建树时间。

---

## 7. Denoiser 专项

### 当前实现

| 项 | 实现 |
|---|---|
| 输入 | 最终的平均颜色（raw HDR） |
| Guide | albedo、法线（`geo_n`，修复 F6 之前对球和 Quad 无效）、深度 `t`；**只用像素中心的一条射线**生成，没有抖动、景深或运动模糊 |
| Kernel | 5×5 B3 样条核，2 个 pass，step = 1, 2，有效半径约 6 px |
| Edge stopping | 法线 `pow(max(dot,0), 128)`、深度 `exp(−|Δt|/(0.02·t))`、albedo `exp(−|Δa|²/0.25)`、颜色 `exp(−|Δ display(c)|²/0.08²)` |
| 颜色项 | 始终以**未滤波的原始颜色**作为参考，每个 tap 都要算 2 次 `display_transform`（包含 `pow`） |
| 调度 | 所有 sample 完成后单线程执行一次 |

### 风险判断

| 风险 | 是否存在 | 原因 |
|---|---|---|
| Blur edge | 低 | 法线指数 128 加上深度项，已经足够严格 |
| **Leak light** | 中 | 阴影边界处 albedo、法线、深度都相同，只能依赖颜色项；低 spp 下颜色项噪声太大，会误判 |
| **Destroy specular** | **高** | 镜面反射和折射中的内容在 guide 里完全不可见（guide 只记录第一次命中的表面），被当作平坦区域滤掉。金属球、玻璃球里反射的环境会被抹平 |
| **Smear texture** | 中 | 没有 albedo demodulation：纹理细节和噪声混在同一个颜色信号里。albedo 项只能在 albedo 差异大时阻止混合，小的纹理对比会被抹掉 |
| **Over-smooth** | 中 | 权重与 spp 无关：1000 spp 的干净图也会被照样滤一遍，损失细节 |
| **DOF / motion blur 边缘** | **高** | guide 只来自一条像素中心的针孔射线；景深虚化区域的 guide 是"清晰"的，与颜色不一致，会产生光晕 |
| 颜色项的无效性 | 中 | σ = 0.08 的 display 空间差异，在低 spp 下噪声本身就超过 σ，退化为"几乎不滤"；在高 spp 下又会过度保留噪声。它不以噪声水平为参考，所以两头都不对 |

### 最值得加的能力（按收益/成本排序）

1. **Guide 与颜色使用同样的样本分布**：每个 sample 都累加 albedo、法线、深度（带抖动、景深、time），最终取平均。成本几乎为 0，直接修掉 DOF 光晕和边缘锯齿。
2. **Albedo demodulation**：滤 `irradiance = color / max(albedo, ε)`，之后再乘回 albedo。纹理不再被抹平，这是离线降噪的标准做法（OIDN 的输入也是这样组织的）。
3. **方差缓冲（luminance moments）**：每个像素累加 `Σ lum` 和 `Σ lum²`，得到样本均值的方差 `σ²/n`；颜色边缘停止改为 `exp(−|Δlum| / (k·√(Var_p + Var_q)))`（SVGF 的核心思路，**不需要**时间累积）。滤波强度会随 spp 自动减弱，1000 spp 时几乎不做滤波。每个 A-Trous pass 同时滤波方差（权重取平方）。
4. **Guide 穿过 delta 表面**：沿镜面或折射链继续追踪，直到第一个非 delta 表面，再记录 albedo 和法线（乘上 delta 的 attenuation）。镜面反射里的内容就不会再被抹平。
5. **拆分 diffuse / specular 缓冲**：在迭代积分器里按第一次反弹的 lobe 分别累加两个缓冲，各自滤波后相加；specular 只做轻微滤波。可以放到 P2。

**不建议**：Temporal history / TAA 式的时间累积。这是离线渲染器，"时间"维度本身就是 progressive 累加，没有需要复用的历史帧。

**验证**：用 64 spp 的降噪结果与 16k spp 的参考图计算 RMSE / relMSE，并与未降噪的 64 spp 对比，确认降噪确实让误差更低，而不只是看起来更平滑。把 variance 输出成 AOV 图，看噪声集中在哪里。

---

## 8. 资产管线

### OBJ → MTL → Material → Texture → PBR 的实际路径

```
tinyobj::ObjReader::ParseFromFile(triangulate = true，fan 三角化)
→ 逐 shape、逐 face（只处理 fv == 3）
   ├─ 顶点位置 / UV / 法线：按 idx 取值；任意一个顶点缺少法线 → 整个面退化为 flat 着色；缺少 UV → (0,0)
   ├─ material_ids[f] → get_or_create_material（按 material id 缓存）
   │    base  = map_Kd（sRGB）或 Kd 常量
   │    normal = norm，否则 map_bump / bump（Linear）  ← 高度图被当成法线贴图（M5）
   │    rough / metal = map_Pr / map_Pm（Linear）；忽略标量 Pr / Pm
   │    有任意 PBR 贴图 → PBR_Material；否则有 Kd → Lambertian；否则 → default_mat
   │    法线约定：按文件名推断（normaldx / _dx / directx）
   └─ make_shared<Triangle>(...)：每个三角形一次堆分配，每个三角形单独计算切线
```

| 检查项 | 状态 |
|---|---|
| Triangulation | ✔ tinyobj 的 fan 三角化；凹多边形会出错（可以用 `triangulation_method = "earcut"`） |
| Smoothing normal | ✔ 使用 `vn`；没有 `vn` 时不会根据 smoothing group 自动生成 |
| Tangent | ◐ 按面计算，不做顶点平滑，没有 handedness（H6） |
| Multiple material | ✔ 按面取 material id 并缓存 |
| Texture path | ◐ `obj_directory / texname`；Windows 反斜杠在 `filesystem` 下可用；**同一张贴图被多个材质引用时会重复加载**（每份 float + byte，约 15 B/像素） |
| Alpha cutout | ✘ 忽略 `map_d` / `d` |
| Negative index | ✔ tinyobj 内部处理 |
| Missing UV | ◐ 回退到 (0,0)，法线贴图会被静默禁用 |
| Missing normal | ◐ 整个面退化为 flat 着色 |
| Emission | ✘ 忽略 `Ke` / `map_Ke`：OBJ 里的发光体无法成为光源 |
| 标量 roughness / metallic | ✘ 忽略 `Pr` / `Pm` |

### 最大限制

**OBJ/MTL 本身没有标准的 PBR 语义。** `Pr`、`Pm`、`norm` 都是 tinyobj 支持的非标准扩展，`bump` 在不同工具里分别表示高度图或法线贴图，导出工具之间互不一致。继续完善 OBJ，得到的只是"对某一个导出工具正确"。

### OBJ 还是 glTF？

**建议：OBJ 只修 M5 中的 3 个语义错误（bump、UV wrap、map_d），然后增加 glTF 2.0（用 cgltf，单头文件）。** 对当前项目，glTF 能带来的具体好处：

1. **metallic-roughness 是 glTF 的原生规范**：`baseColorTexture`（sRGB）、`metallicRoughnessTexture`（Linear，**G 通道 = roughness，B 通道 = metal**）、`normalTexture`（带 `scale`，OpenGL 约定）、`emissiveTexture`（sRGB）+ `emissiveFactor`。当前的 `PBR_Material` 可以一一对应，不需要靠文件名猜测。
2. **切线自带 handedness**：`TANGENT` 是 vec4，`w` 就是 handedness，正好解决 H6-3。
3. **`alphaMode = MASK` + `alphaCutoff`**：alpha cutout 有标准定义。
4. **Khronos glTF-Sample-Assets** 提供了一批**可以作为验证基准**的资产：`MetalRoughSpheres`（roughness/metal 网格，可以直接替代手搓的 PBR 验证场景）、`NormalTangentMirrorTest`（专门测试 H6-3 的镜像 UV）、`AlphaBlendModeTest`、`Sponza`（带 PBR 贴图的版本）。**这是 OBJ 生态提供不了的。**
5. 有 node 层级和 TRS 矩阵：正好推动 `Rotate_Y` / `Scale` / `Translation` 升级成通用的 4×4 `Transform`。
6. 相机和 KHR_lights_punctual 可以让场景脱离硬编码的 `Renderer.cpp`。

---

## 9. Color Management

### 各类数据应该怎么处理

| 数据 | 应该 | 当前（修复后） | 问题 |
|---|---|---|---|
| HDRI（`.hdr` / `.exr`） | 线性，原样使用 | `.hdr` 走 stb 的 HDR 通道，线性 ✔；`srgb_input` 选项用于 LDR 环境贴图 ✔（F1 修复之后才对） | 不支持 EXR（多数 HDRI 以 EXR 分发） |
| baseColor / albedo | sRGB → linear | `color_space::SRGB` ✔（F1 修复之后才对） | 8-bit 量化后再解码，暗部有色带（可接受） |
| normal | Linear，`2x−1`，**不做**任何 gamma | ✔（F1 修复之后才对） | 最近邻采样，法线不会重新归一化插值（可接受） |
| metallic / roughness | Linear，单通道 | 取 R 通道 ✔ | glTF 的打包贴图应该取 B / G 通道 |
| emission | sRGB 颜色 × 强度（线性） | 只有常量 | — |
| Procedural（Perlin / Checker） | 视为线性 | 线性 ✔ | — |
| 光照计算 | 线性 RGB（Rec.709 原色） | ✔ | — |
| 累加 | 线性 HDR，double | ✔ | — |
| Tone mapping | 曝光 → 显示变换（ACES / AgX / Reinhard-luminance） | 逐通道 Reinhard，无曝光 | L5 |
| Output | sRGB OETF → 8-bit；另外输出一份线性 HDR 格式 | 只有 8-bit PPM | 没有 HDR 输出，**无法做数值比对**（§11） |

### 已确认被错误 sRGB 转换的纹理（已修复）

修复 F1 之前：**所有** LDR 纹理都先被 stb 做了一次 `pow(2.2)`。

- baseColor 实际等于 `srgb_to_linear(x^2.2)`：解码了两次，颜色偏暗、饱和度偏高。
- normal、roughness、metallic 实际等于 `x^2.2`：法线整体倾斜，roughness 被压低（金属看起来更光滑），metallic 的中间值被压向 0。
- 用 `srgb_input = true` 的 LDR 环境贴图也被解码了两次。

---

## 10. 架构审查

判断原则：只在当前结构已经**明确阻碍**功能扩展时才建议修改。

| 问题 | 是否已构成限制 | 证据 | 建议 |
|---|---|---|---|
| **积分器绑定在 `Camera` 上** | **是** | `Camera` 同时负责相机、两套 `ray_color`（有 / 无 lights）、MIS、RR、光源 PDF 构建、渐进循环、tile、预览、降噪调度、文件输出。`environment` 是 `Camera` 的成员。增加任何一种 debug 输出或新积分器，都要在 `Render` / `RenderProgressive` × 有 / 无 lights 的 4 个 lambda 里各复制一遍 | 拆出 `Scene` / `Integrator` / `Renderer`（P1-2），见下 |
| 递归 + 没有显式 throughput | **是** | 没法做基于 throughput 的 RR，没法做路径日志，也没法拆分 AOV（direct/indirect、diffuse/specular） | 迭代式 PathIntegrator |
| `PDF` 堆对象树 | 是（性能） | P-1 | 栈上 `BSDF` 值类型 |
| 光源采样挂在 `Hittable` 上 | 是 | M2：变换和网格都无法作为光源；`lights` 同时承担"采样目标"和"发光体"两种语义（M10） | 独立的 `Light` 列表：`AreaLight{ const Hittable* shape; Le }` + `EnvironmentLight` |
| 两套 `ray_color`（有 / 无 lights） | 是 | 代码重复；无 lights 的版本其实就是 lights 为空时的特例 | 合并 |
| `Material` 虚接口 | 否 | 新增材质只需要实现 `Scatter` / `Eval`，并不困难 | 保留；删除死代码（L7）。等到做 BSDF 值类型时再调整 |
| `Texture` 虚接口 | 否 | 调用次数高，但扩展方便 | 保留 |
| `Hittable` + 变换装饰器 | 部分 | 只支持 `Rotate_Y`；变换不能转发光源采样 | 做 glTF 时统一成 4×4 `Transform` |
| `Renderer.cpp` 的 `switch(19)` | 是（工程） | 换场景要重新编译；无法批量跑回归测试 | 最小 CLI：`--scene name --spp N --seed S --out f.pfm --aov normal` |
| `PostProcess` 接口 | 否 | 设计合理 | 保留 |

### 第十二阶段：Integrator 抽象是否值得？

**值得，而且是当前阶段收益最高的一次重构**，因为它同时直接支撑了：

- **Debug**：`NormalIntegrator`、`AlbedoIntegrator`、`DepthIntegrator`、`DirectOnlyIntegrator` 都只有 10–20 行，复用同一个 `Scene` 和同一个 Renderer 循环。
- **Testing**：白炉测试和回归测试只需要 `Scene + Integrator + seed → 线性 buffer`，不需要窗口、PPM 或 tonemap。
- **Feature**：新增光源类型、新的采样策略，只需要改积分器；AOV 拆分（direct/indirect、diffuse/specular）只需要在路径循环里多写几个缓冲。

建议的最小形态（**不要引入更多层**）：

```cpp
struct Scene      { const Hittable* accel; std::vector<Light> lights; const Environment* env; };
struct PixelSample{ Color L; AOVs aov; };          // aov: albedo / normal / depth / direct / indirect ...
class  Integrator { public: virtual PixelSample Li(const Ray&, const Scene&, Sampler&) const = 0; };
class  Renderer   { /* tiles, progressive, accumulation, variance, output；Camera 只负责 GenerateRay */ };
```

整个框架只需要 `Integrator` 这一个 virtual（每个样本调用一次，性能上可以忽略）。

---

## 11. 测试能力

**当前状态：完全没有测试。** `Pi.cpp` 是 RTIOW 的练习，不在工程里。

本次审查发现的 bug 中，能被以下测试提前抓到的：F1（纹理解码测试）、F2 和 F3（体积白炉）、F5 和 H1（GGX 白炉 + PDF 归一化）、F7（光源朝向检查）、M1（权重打印）。**全都可以。**

### 11.1 数学 / BSDF 单元测试（个人项目规模，不引入框架）

新增一个 `tests.cpp`（独立的 exe 或 `--test` 参数），用 `CHECK(cond, msg)` 宏即可：

| 测试 | 方法 | 通过条件 |
|---|---|---|
| ONB | 随机法线 → 三轴正交且单位长度，`transform(local(v)) == v` | 误差 < 1e-9 |
| Reflect / Refract | Snell 定律；`refract` 的全内反射边界；`reflect` 的对合性 | 解析值 |
| AABB | 已知射线的命中与未命中；平行于某个面的射线；`inv_dir = ±inf` | 布尔值 |
| Transform | `Rotate_Y` / `Scale` 之后，法线与切线保持正交；与数值有限差分对比 | < 1e-6 |
| **PDF 归一化** | 对每个 `PDF`：`∫pdf dω ≈ 1`（均匀球面 MC 或确定性网格）；`E_{ω~pdf}[1/pdf] = 支撑集立体角` | 3σ 以内 |
| **Sample/PDF 一致性** | 采样得到的方向直方图与 `pdf` 的解析值做 χ² 检验（θ×φ 网格，参考 Mitsuba 的 chi2test） | p > 0.01 |
| **白炉（BSDF 层）** | `E[f·cos/pdf]` 对 (roughness, metallic, cosθv) 做网格扫描 | ≤ 1 + 3σ；电介质 F0 = 0 时 ≈ 1；金属与精确 Smith 的参考值一致 |
| Fresnel | Schlick 在 0° / 90° 的端点值；Dielectric 的 R + T = 1 | 解析值 |
| 纹理解码 | 生成 1×1 的 PNG（值 128），分别按 SRGB / Linear 读取 | 0.2158 / 0.5020 |
| 环境贴图 | 确定性网格积分 `∫pdf dω` | = 1 ± 1e-4（本次审查实测 1.00000） |
| 光源朝向 | 场景构建后，从场景包围盒中心检查每个单面发光体的 `front_face` | 至少一个 `true`，否则打印警告 |

### 11.2 渲染回归（小而确定）

**前提（P0-1）**：可以固定 seed 的 RNG；可以输出线性 float 的 PFM；最小 CLI。

```
tests/
  scenes.cpp           // 6 个小场景，都用代码构建，不依赖外部资产（或只依赖仓库内资产）
  reference/*.pfm      // 在同一台机器上用 16k spp 生成一次，提交到仓库（160×90 × 12 B ≈ 170 KB/张）
  run: PathTracer-CPP.exe --regress [--update-references]
```

| 场景 | 分辨率 / spp | 作用 |
|---|---|---|
| `furnace_lambert` | 64×64 / 256 | 均匀环境光 L = 1 下的白色 Lambert 凸物体：每个像素**解析值 = 1**（凸物体只有一次反弹）|
| `furnace_volume` | 64×64 / 1024 | 均匀环境光下 albedo = 1 的体积球（边界不加入 world）：**解析值 = 1**。能直接抓到 F2 / F3 |
| `furnace_metal_sweep` | 256×32 / 1024 | 一排 roughness 从 0 到 1 的白色金属球：与单次散射的解析反照率曲线对比。能直接抓到 H1 |
| `cornell_small` | 128×128 / 64 | NEE + MIS + 面积光的回归 |
| `pbr_ibl_small` | 160×90 / 64 | 环境光重要性采样 + PBR + 法线贴图 |
| `glass_mesh` | 128×128 / 64 | 三角网格玻璃，用来防止 H4 回归 |

**指标**（全部在**线性** HDR 上计算，不经过 tonemap）：

- `MSE`、`RMSE`；
- **`relMSE = mean((x−r)² / (r² + 0.01))`**：HDR 图像上最有用的指标，不会被高亮像素主导；
- `PSNR`：在 tonemap 之后的 8-bit 上计算，只作为参考；
- 白炉场景：`|mean − 1| < 3·σ/√N`（用 variance buffer 估计 σ）。

**判定**：`relMSE_test < 1.5 × relMSE_expected`（expected 值取首次运行时的结果，存进 JSON）。失败时输出 `|diff|` 热力图 PFM/PPM。

**额外产出（作品集价值很高）**：`RMSE vs 时间` 曲线，用来证明每一项采样改进（M1、N2、M4、低差异序列）"在同样的时间里误差更低"。

---

## 12. Debug Visualization

目标："**为什么这个像素是黑的**"要能被程序直接回答。

### 值得做的 AOV（按定位问题的价值排序）

| AOV | 能回答什么 | 成本 |
|---|---|---|
| **单像素路径追踪日志** `--debug-pixel x y` | 每次反弹：命中的图元和材质、`n`/`geo_n`/`n_s`、wo/wi、f、cos、pdf_bsdf、pdf_light、MIS 权重、throughput、RR 概率、NaN 出现的位置。**这是回答"为什么是黑的"最直接的工具** | 低（迭代积分器里加 `if (logger)`） |
| **NaN/Inf 掩码** | 哪些像素被非有限值污染，以及第一次出现在哪一次反弹 | 极低 |
| **Variance / sample count** | 噪声集中在哪里；降噪器是否需要 | 低（本来就需要，见 §7） |
| Albedo / Normal（shading 与 geo 分开） / Depth | 纹理解码是否正确（F1）；法线贴图是否生效（H6）；`geo_n` 是否被正确写入（F6） | 极低 |
| Roughness / Metallic | 贴图通道是否读对了（例如 Metal1 的 roughness 贴图接近全黑） | 极低 |
| **Direct / Indirect 分开** | 暗部是缺直接光（NEE 或光源朝向，例如 F7）还是缺间接光 | 低 |
| **BVH 访问热力图** | 每条射线的节点访问数与图元测试数；SAH 是否生效 | 低 |
| Light PDF / BSDF PDF / MIS 权重（bounce0） | M1 这类权重问题：看某一种策略是否几乎不起作用 | 低 |
| `front_face` / backface 命中 | 单面光源朝向（F7）；网格法线翻转 | 极低 |

### 实现方式

- 走 `Integrator` 抽象：`AOVIntegrator(kind)` 做首次命中类的 AOV；PathIntegrator 在路径循环里顺带写 direct/indirect、MIS 等缓冲。
- 输出为 PFM（线性）+ 自动归一化的 PPM 预览（法线映射到 [0,1]，热力图用 viridis 或 turbo 色表）。
- 预览窗口加一个按键，在不同 AOV 之间切换，对展示和调试都很有用。

---

# Rendering Quality Improvements

真正能改善画面的技术，按"收益 / 成本"排序：

1. **修正 Smith G**（H1）：PBR 金属边缘恢复正确亮度。约 10 行。
2. **光源选择权重**（M1）：面积光的直接光方差下降数倍。约 15 行。
3. **Continuation ray 兼做 BSDF MIS + 基于 throughput 的 RR + 按能量选 lobe**（N2、N7、M4）：同样时间下 RMSE 更低。
4. **球的切线 + TBN handedness**（H6）：展示图里的法线贴图真正生效；镜像 UV 的资产不再出错。
5. **降噪升级**（§7）：guide 使用同样的样本分布 + albedo demodulation + 方差引导。DOF 光晕、纹理抹平、过度平滑这三个问题一起解决。
6. **纹理 wrap 模式 + 双线性过滤**：Sponza、glTF 资产的正确性前提。
7. **射线偏移的鲁棒化**（H4）：三角网格玻璃，以及不同尺度的场景。
8. **曝光参数 + ACES/AgX 显示变换 + HDR 输出**（L5）：高光不再偏色，并且保留线性数据。
9. **Kulla-Conty 能量补偿**（M3）：粗糙金属不再偏暗。在修正 Smith G 之后再做。
10. **粗糙介质透射**（Walter 2007，GGX BTDF）：玻璃从 delta 扩展到磨砂玻璃，展示效果明显。P2。

---

# Engineering Improvements

1. **确定性**：按 (pixel, sample) 派生种子的 RNG；Perlin 和场景随机布局都使用固定 seed。
2. **线性输出**：PFM（几十行代码，无依赖）；需要时再用 tinyexr 输出 EXR。
3. **最小 CLI**：`--scene`、`--spp`、`--seed`、`--width`、`--out`、`--aov`、`--debug-pixel`、`--regress`、`--threads`（通过线程数限制或 affinity 实现）。
4. **测试**：§11 的数学、BSDF 和回归测试。
5. **Integrator / Scene / Renderer 拆分**（§10）。
6. **统计计数器**：射线数（按类型）、MRays/s、每条路径的平均反弹数、每条射线的 BVH 节点访问数、NaN 计数、分配次数（benchmark 构建）。渲染结束时打印出来。
7. **构建**：补全 `CMakeLists.txt`（当前是空文件）；工程文件的 toolset 与 README 保持一致；打开 `/W4` 并清理 C4244 这类告警。
8. **资产**：OBJ 修正 3 个语义错误；新增 glTF（§8）；纹理按路径缓存；让 `.gitignore` 允许提交子目录中的 `Model/**/*.obj`，并把测试场景需要的小资产放进仓库。
9. **README**：修正 §2.4 列出的 10 处不一致；给出实际的构建命令，以及"如何复现展示图"的步骤。

---

# Portfolio Improvements

站在面试官的角度：功能列表本身已经足够长，**区分度不在于再加一个功能**，而在于能否证明下面这些：

| 维度 | 现在 | 做完后 | 面试时能讲的内容 |
|---|---|---|---|
| **数学正确性** | 没有证据 | 白炉测试 + PDF 归一化 + χ² 检验的结果表 | "我发现并修复了 Karis G / 双重 gamma / 相位函数的 bug，下面是修复前后的白炉数据" —— **非常有说服力** |
| **可复现** | 不可复现 | 固定 seed + PFM + 回归测试 | "任何改动都会跑 6 个参考场景，relMSE 超过阈值就失败" |
| **性能分析** | 2.18× / 24 核（负面） | 扩展曲线、分配计数、MRays/s、BVH 热力图 | "我定位到 `shared_ptr` 的原子竞争和热路径分配，把扩展性从 2.2× 提升到 N×" —— **典型的工程故事** |
| **采样 / 方差** | 只看肉眼 | 同等时间的 RMSE 曲线 | "修正光源权重之后同样时间 RMSE 降低 X%"，这是用数据说话的证据 |
| **Debug 能力** | 没有 | AOV + 单像素路径日志 | 现场演示"这个像素为什么是黑的" |
| **架构** | `Camera` 上帝类 | Scene / Integrator / Renderer | 可以讲清楚"为什么拆、只拆了哪些" |
| **资产** | 手搓场景 + 缺失的 OBJ | glTF 样例资产（MetalRoughSpheres、Sponza） | 业界公认的测试资产，结果可以和其他渲染器交叉对比 |
| **文档** | 功能列表 | 技术说明 + 修复前后对比 + 已知限制 | "我知道它哪里不对"，这本身就是资深工程师的信号 |

**最有价值的一张图**：同一个场景，左边是修复前、右边是修复后，下面附上白炉测试的数值表。它比任何一个新功能都更能体现图形学功底。

---

# Not Worth Doing Yet

| 技术 | 为什么现在不值得 |
|---|---|
| **ReSTIR** | 为实时渲染中大量光源 + 少量 spp 设计；本项目是离线渲染，只有 1–2 个光源 |
| **MLT** | 实现和调试成本极高；对当前场景（开放场景 + 环境光）收益很小；焦散问题靠场景设计就能避开 |
| **BDPT** | 同上。只对"光源被封闭"的场景有明显收益；在积分器没有迭代化、没有测试之前，几乎不可能做对 |
| **Spectral rendering** | 当前没有色散、薄膜或荧光需求，RGB 已经足够；并且需要改动所有颜色路径 |
| **Subsurface** | 需要 random-walk 体积和介质栈，而目前的体积连能量都才刚修正确 |
| **Heterogeneous volume** | 需要 delta / ratio tracking 和 VDB 加载；应该先用白炉测试验证 homogeneous volume |
| **SIMD traversal / 4-wide BVH** | 在 P-1、P-2 解决之前，瓶颈不在遍历上；标量 SAH + 扁平 BVH 就能拿到大部分收益 |
| **Custom thread pool / work stealing** | P-11：调度不是问题 |
| **AoS → SoA** | P-12：没有 SIMD 就没有收益 |
| **Hierarchical env sampling / alias table** | N10：2D CDF 已经正确，alias table 只节省一次二分查找 |
| **Embree** | 能大幅提速，但会让"自己写的 BVH"失去展示价值。可以作为**对照组**（"我的 BVH 达到了 Embree 的 X%"），但不建议替换 |
| **Sheen / Anisotropy** | 在当前的展示场景中几乎看不出效果；作为材质扩展练习，价值低于修正现有 GGX |
| **Temporal accumulation / TAA** | 离线渲染不需要 |

---

## 17. 未来图形学特性逐项评估

| 特性 | 结论 | 理由 |
|---|---|---|
| SAH BVH | **P1**（与扁平化一起做） | 对网格资产有 2–3× 的收益，对当前展示场景接近 0（§6） |
| BVH Flattening | **P1** | 消除指针追逐和 virtual 调用，和 P-6 一起做 |
| SIMD Traversal | ✘ 暂不 | 见上表 |
| Russian Roulette | 已有；**P1** 改为基于 throughput | N7 |
| Better MIS | **P0/P1** | M1 + N2：真正的问题是光源权重和射线预算，而不是 heuristic 本身 |
| Multiple Lights | ◐ | 已经支持多个光源（均匀选择）；把均匀选择改为按功率选择，与 M1 一起做 |
| Alias Table | ✘ | 光源数 ≤ 10 时，线性 CDF 就够用 |
| Env IS Improvement | 只修 L4 | 已经正确 |
| glTF | **P1/P2** | §8 |
| Alpha Cutout | **P2**（与 glTF 一起） | 需要 any-hit 回调，在扁平 BVH 里更容易实现 |
| Clearcoat | P3 | 分层模型的能量处理比较有难度；展示价值中等 |
| Anisotropy | ✘ 暂不 | 需要切线场，而切线（H6）都还没有做好 |
| Sheen | ✘ 暂不 | 展示价值低 |
| Subsurface | ✘ 暂不 | 见上表 |
| Microfacet Transmission | **P2** | 磨砂玻璃，数学上与已有的 VNDF 一脉相承，展示价值高 |
| Participating Media（homogeneous + NEE） | P2 | 当前体积内没有 NEE（体积散射点也可以做光源采样，已经有接口）；加上 HG 相位函数 |
| Heterogeneous Volume | P3 | — |
| Spectral Rendering | ✘ | 见上表 |
| EXR | **P0 先做 PFM**，P2 再做 EXR | 测试需要线性输出；EXR 用于对外交付 |
| ACES / AgX | P2 | L5；很便宜，但必须先有曝光参数 |
| Adaptive Sampling | P2（依赖 variance buffer） | 用方差估计把样本分给噪声高的像素；对"天空 + 金属"这类场景很有效 |
| Variance Estimation | **P1** | 降噪、自适应采样、测试判定都依赖它 |
| BDPT / MLT / ReSTIR | ✘ | 见上表 |
| OIDN | **P2**（作为对照） | 5 行集成就能得到业界基线；可以和自己的 A-Trous 对比 RMSE。**对照组，而不是替代品** |
| Embree | P3（作为对照组） | 同上 |

---

# Recommended Roadmap

> 每项包含：问题 / 涉及文件 / 技术原理 / 复杂度 / 预期收益 / 验证。

## P0 — 应该先修（正确性 / 基础设施）

### P0-1 可复现的基础设施：确定性 RNG + PFM 输出 + 最小 CLI

- **问题**：结果不可复现，没有线性输出，没法做任何数值比对。后面所有工作都依赖它。
- **文件**：`My_Common.h`（RNG）、`Camera.h`（在每个样本开始时设置种子）、新增 `ImageIO.h`（PFM）、`Renderer.cpp`（参数解析）、`Perlin_Noise.h`。
- **原理**：`thread_local` 的 PCG32 在每个 (pixel_index, sample_index) 开始时用 `hash(pixel, sample, seed)` 重新设置种子。这样 `random_double()` 的调用点完全不用改，结果也与线程调度无关。PFM 是 `"PF\nW H\n-1\n"` + 从下到上的 float RGB。
- **复杂度**：低（约 150 行）。
- **收益**：同一个 seed 渲染两次结果逐位相同；所有后续工作都可以量化。
- **验证**：同一个 seed 连续两次渲染，逐字节比较；切换 serial/parallel 后结果也一致。

### P0-2 修正 Smith G（H1）+ M3 中的两个小问题

- **问题**：光滑金属在掠射角最多损失 80% 能量；电介质在掠射角增能 6%；分母上的 `+1e-4` 造成偏差。
- **文件**：`Material.h` `PBR_Material::geometry_smith`、`cook_torrance_specular`。
- **原理**：使用精确的 separable Smith，`G1 = 2/(1+√(1+α²tan²θ))`，α = r²（与 `GGX_PDF::Smith_G` 保持一致），或者 height-correlated 形式 `1/(1+Λv+Λl)`，其中 `Λ = (−1+√(1+α²tan²θ))/2`。去掉 `+1e-4`。
- **复杂度**：低（约 20 行）。
- **收益**：展示图中金属球的边缘恢复正确亮度，并且和 VNDF 采样属于同一个微表面模型，采样权重的方差也更低。
- **验证**：§11.1 的 BSDF 白炉扫描。r = 0.05 时所有 cosθv 下的反照率都应 ≥ 0.99；m = 0 时反照率应 ≤ 1。

### P0-3 光源选择权重（M1）+ 不可采样光源的防护（M2）+ Cornell 的 lights（M10）

- **问题**：面积光实际只分到 5% 的样本；不可采样的"光源"被静默丢弃；玻璃球被当成光源。
- **文件**：`Camera.h` `build_light_pdf`、`Environment.h` `sampling_power_estimate`、`Hittable_List.h`、`Renderer.cpp` `Cornell_Box`。
- **原理**：env 的估计值取 `intensity·total_weight·2π²/(W·H)`，也就是 `∫L dω`，与分辨率无关；geo 的估计值取 `Σ L_i·A_i·|cos|/d²`（每个着色点计算一次）或全局常数；`lights` 为空或功率为 0 时直接跳过对应的 PDF。
- **复杂度**：低（约 40 行）。
- **收益**：README_Showcase 和 PBR_IBL_Test 中面积光的直接光方差下降数倍（预期同样 spp 下 RMSE 明显降低）。
- **验证**：打印 `w_geo`（应在 0.3–0.7 之间）；在 `pbr_ibl_small` 上比较同样 spp 下相对于参考图的 RMSE。

### P0-4 最小测试集（§11.1 + 三个白炉场景）

- **问题**：没有任何正确性保护；本次修复的 bug 很容易再次出现。
- **文件**：新增 `Tests.cpp`（或 `--test` 参数）、`Environment.h`（新增 `Constant_Environment`，L = 1，pdf = 1/(4π)）。
- **原理**：见 §11。
- **复杂度**：低到中（约 300 行）。
- **收益**：本次审查中的所有发现都会变成可以自动检查的断言；这也是作品集里最有说服力的材料。
- **验证**：测试本身。先把 F1–F7 的旧代码临时恢复，确认测试会失败，再切回修复后的代码确认测试通过（证明测试确实有效）。

### P0-5 NaN/Inf 防护 + 射线偏移鲁棒化（M6、H4）+ README 修正

- **问题**：一个 NaN 就会造成永久黑点；三角网格玻璃自相交；README 有 10 处与代码不一致。
- **文件**：`Camera.h`（累加前检查）、`Triangle.h`（去掉求交内部的偏移）、新增 `offset_ray_origin()`、`README.md`。
- **原理**：非有限值的样本直接丢弃并计数；新射线的起点沿 `geo_n` 向出射方向一侧偏移，偏移量随坐标尺度缩放。
- **复杂度**：低。
- **收益**：消除一整类"莫名其妙的黑点"；网格玻璃可以正常工作。
- **验证**：NaN 计数器输出 0；`glass_mesh` 回归场景（三角化的球 vs 解析球，RMSE 应接近 0）。

## P1 — 最值得做（收益很高）

### P1-1 Debug AOV + 单像素路径日志

- **问题**："为什么这个像素是黑的"目前只能靠猜。
- **文件**：`Camera.h` / 新增 `Integrator.h`、`PostProcess.h`（AOV 缓冲）。
- **原理**：见 §12。
- **复杂度**：低到中。
- **收益**：大幅提升调试效率；也是很好的演示材料。
- **验证**：用法线 AOV 确认 H6 修复后球面上能看到贴图细节；用 `--debug-pixel` 打印一条命中面积光的路径，逐项核对 MIS 权重之和等于 1。

### P1-2 迭代式 PathIntegrator + 栈上 BSDF（Integrator 抽象）

- **问题**：P-1、P-2、P-3、P-4、N2、N7、M4 共享同一个根源：递归结构 + 堆上的 PDF 树 + 积分器绑定在 `Camera` 上。
- **文件**：新增 `Integrator.h`、`Scene.h`、`BSDF.h`；`Camera.h` 精简为只负责生成射线 + 渲染循环；`Material.h` 增加 `GetBSDF(rec) → BSDF` 值类型。`HitRecord::mat` 改为 `const Material*`。
- **原理**：
  ```
  β = 1; specular_bounce = true; pdf_prev = 0
  loop:
    hit? no → L += β·w_env(pdf_prev)·Le_env; break
    Le：若 specular_bounce 或是相机射线 → L += β·Le；否则 L += β·w_bsdf(pdf_prev, pdf_light)·Le
    bsdf = mat.GetBSDF(rec)       // 只在这里求一次纹理
    非 delta：NEE（1 条阴影射线，MIS）
    采样 bsdf → wi, f, pdf, is_delta；β *= f·|cos|/pdf
    RR（bounce ≥ 3）：q = max(0.05, 1 − max(β))；β /= (1 − q)
  ```
- **复杂度**：中（约 400 行，两个晚上到一个周末）。
- **收益**：每条路径的堆分配从几十次降到 0；每次反弹少 1 条射线；lobe 选择按能量进行；线程扩展性显著改善（**预期**，需要用扩展曲线验证）；AOV 和日志都变得很自然。
- **验证**：P0-4 的回归场景的 relMSE 在噪声范围内与参考图一致（证明无偏）；对比分配计数（目标 0）、MRays/s、1–24 核的扩展曲线，以及同等时间下的 RMSE。

### P1-3 降噪升级：guide 使用相同的样本分布 + albedo demodulation + 方差引导

- **问题**：§7 中的 DOF 光晕、纹理被抹平、镜面反射被破坏、高 spp 下仍然过度平滑。
- **文件**：`Camera.h` / Renderer（每个样本累加 guide、`Σlum` 和 `Σlum²`）、`PostProcess.h`。
- **原理**：见 §7 的第 1–4 条。
- **复杂度**：中。
- **收益**：低 spp 预览的画质明显更好；高 spp 下自动退化为几乎不做滤波。
- **验证**：64 spp 降噪结果相对 16k spp 参考图的 relMSE，应低于未降噪的 64 spp，且低于当前降噪器的结果；variance AOV 的可视化。

### P1-4 SAH + 扁平 BVH + mesh 索引布局

- **问题**：median split 加上指针追逐的递归遍历；每个三角形 400+ 字节。
- **文件**：`BVH.h`（重写）、`Triangle.h` → `Mesh.h`、`ObjLoader.h`。
- **原理**：见 §6。
- **复杂度**：中。
- **收益**：网格场景提速 2–3×（Sponza 类场景），建树时间下降，内存下降一个数量级。
- **验证**：节点访问数热力图；MRays/s；和旧 BVH 的渲染结果做像素级比较（在相同 seed 下，结果应当逐位一致或 relMSE ≈ 0）。

### P1-5 法线贴图补全：球的切线 + handedness + 顶点切线（H6）

- **问题**：展示图中的法线贴图不生效；镜像 UV 资产出错；网格上出现切线接缝。
- **文件**：`Sphere.h`、`Triangle.h` / `Mesh.h`、`Material.h` `sample_shading_normal`、`ObjLoader.h`。
- **原理**：见 H6；每个顶点保存 `T.xyz + w`，在着色时用 `B = w·cross(N, T)` 重建。
- **复杂度**：低到中。
- **收益**：README 中宣称的能力真正成立。
- **验证**：法线 AOV；glTF 的 `NormalTangentMirrorTest`（在 P2-1 之后），或者手工构造一个左右镜像 UV 的 panel。

## P2 — 有时间再做

- **P2-1 glTF 2.0 加载（cgltf）+ 纹理 wrap 与双线性过滤 + alpha cutout**
  - 问题：§8。涉及文件：新增 `GltfLoader.h`、`Texture.h`、BVH 的 any-hit。
  - 复杂度：中。收益：可以使用标准验证资产，也可以渲染带 PBR 贴图的 Sponza。
  - 验证：`MetalRoughSpheres` 与 Khronos 的参考截图对比；`AlphaBlendModeTest`。
- **P2-2 低差异序列采样器**（Sobol + Owen scrambling，或 R2 + 按像素的 Cranley-Patterson 旋转）
  - 涉及文件：`Sampler.h`。依赖 P0-1 的维度化接口。
  - 复杂度：中。收益：直接光为主的场景，同等 spp 下 RMSE 一般会有可测的下降。
  - 验证：RMSE-vs-spp 曲线的斜率。
- **P2-3 粗糙介质透射**（Walter 2007 的 GGX BTDF，配合 VNDF）
  - 涉及文件：`Material.h`。
  - 复杂度：中。收益：磨砂玻璃，展示效果明显。
  - 验证：白炉测试中 R + T ≤ 1；roughness → 0 时的极限应与 `Dielectric` 一致。
- **P2-4 自适应采样**：依赖 variance buffer。按像素的相对误差分配额外样本。
  - 验证：同等时间下 relMSE 更低。
- **P2-5 显示变换 + 曝光 + EXR**（tinyexr）。
- **P2-6 体积内的 NEE + HG 相位函数**：体积散射点也调用光源采样；用 `furnace_volume` 回归场景保护正确性。
- **P2-7 Kulla-Conty 能量补偿**：粗糙金属在白炉测试中应接近 1。
- **P2-8 OIDN 作为对照组**：和自己的 A-Trous 对比 relMSE，写进 README。

## P3 — Research / Optional

- Clearcoat（带能量守恒的分层）；
- Heterogeneous volume（delta tracking + ratio tracking，NanoVDB）；
- 4-wide BVH + SIMD 遍历（在 P1-4 之后，用 profiler 数据证明遍历已成为瓶颈时再做）；
- Embree 作为对照组；
- Guided path tracing（Müller 2017 的 SD-tree），适合"看得见玻璃后面光源"的场景，但研究属性较强。

---

## 18. 最终筛选：最多 10 个真正值得继续开发的项目

| # | 项目 | 为什么当前项目需要它 |
|---|---|---|
| 1 | **P0-1 确定性 RNG + PFM + CLI** | 没有它，其余 9 项都无法量化验证 |
| 2 | **P0-2 修正 Smith G** | 展示图里的金属是错的，而且修起来只要 10 行 |
| 3 | **P0-3 光源选择权重** | 当前面积光只分到 5% 的样本，是展示场景最主要的方差来源 |
| 4 | **P0-4 白炉 / PDF / 回归测试** | 本次审查的每个 bug 都能被它抓到；也是作品集中最能说明问题的材料 |
| 5 | **P1-2 迭代 PathIntegrator + 栈上 BSDF** | 一次重构同时解决分配、竞争、射线浪费、lobe 选择、RR 和 AOV 的扩展性 |
| 6 | **P1-1 Debug AOV + 单像素路径日志** | 直接回答"这个像素为什么是黑的" |
| 7 | **P1-3 降噪升级（方差 + demodulation + 样本一致的 guide）** | 当前降噪会破坏镜面反射、抹平纹理，在 DOF 区域产生光晕 |
| 8 | **P1-5 法线贴图补全** | README 宣称的能力在展示图中并未生效 |
| 9 | **P1-4 SAH + 扁平 BVH** | 渲染真实网格资产的前提 |
| 10 | **P2-1 glTF** | 有标准 PBR 语义和公认的验证资产 |

---

## 19. 如果只能再投入 1–2 周：最值得做的 3 件事

### ① 可复现 + 可验证（P0-1 + P0-4 + P0-2 + P0-3）｜约 3–4 天

先把确定性 RNG、PFM 和 CLI 做出来，然后写 PDF 归一化、BSDF 白炉，以及 `furnace_lambert`、`furnace_volume`、`furnace_metal_sweep` 三个场景的测试；在测试的保护下修正 Smith G 和光源权重。

**产出**：一张"修复前后"的白炉数据表加上对比图，以及一个回归测试脚本。**这是作品集中最能体现数学正确性的部分，成本也最低。**

### ② 迭代 PathIntegrator + 栈上 BSDF + Debug AOV（P1-2 + P1-1）｜约 4–5 天

去掉递归和堆上的 PDF，`HitRecord` 改用裸指针；每次反弹 1 条阴影射线 + 1 条 continuation ray；RR 基于 throughput；lobe 按能量选择；同时加上 Normal / Albedo / Direct / Indirect / Variance / NaN AOV 和 `--debug-pixel`。

**产出**：线程扩展曲线（修改前后对比）、每条路径的分配次数（→ 0）、同等时间下的 RMSE 对比。**这是作品集中最能体现工程和性能能力的部分。**

### ③ 方差引导的降噪 + 法线贴图补全（P1-3 + P1-5）｜约 3 天

每个样本都累加 guide 和 `Σlum²`，加上 albedo demodulation 和方差边缘停止；补上球的切线和 handedness。

**产出**：64 spp 降噪相对参考图的 relMSE 对比表；**README 展示图第一次真正带上法线贴图细节**。这是画质上最直观的提升。

> 这三件事完成后，项目会从"功能很多的学习型 Path Tracer"，变成"能证明自己是对的、知道瓶颈在哪、有工具定位问题"的渲染器。这才是面试官真正想看到的。
