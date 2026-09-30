# PathTracer-CPP 实现报告

> 分支：`feat/pathtracer-improvements`（基于 `main` @ `6208633`）
> 输入：`PATH_TRACER_IMPROVEMENT_AUDIT.md`
> 测试机器：Intel Core Ultra 7 265KF（20 线程，8P + 12E），Windows，Visual Studio 2022 17.14 / MSVC 19.38（v143），Release x64
> 文中所有数字都来自本机的实际运行，原始输出保存在 `build/`（未提交）。

---

## 1. Completed Work

| 阶段 | Commit | 内容 |
|---|---|---|
| 0 | `74c3601` | 提交上一阶段审查中已完成但尚未提交的修复 F1–F14 |
| 0 | `67df9f4` | 工程文件的 toolset 从写死的 `v145` 改为 `$(DefaultPlatformToolset)` |
| 1 Safety net | `fa6f6c0` | 确定性 RNG（PCG32，按 (seed, pixel, sample) 派生随机流）、PFM 输出、CLI、单元测试、回归测试框架 |
| 2 Correctness | `5cc2bb4` | 精确的 height-correlated Smith G2（H1）+ 能量守恒的漫反射耦合（M3） |
| 2 | `d7f68db` | 量纲一致的光源选择概率（M1）、光源列表校验（M2）、球形光源 NaN（M9）、Cornell_Box 的 lights（M10） |
| 2 | `2a80474` | `Sphere` 切线空间（H6-1）、`ASSET_REVIEW.md`、`--image-stats`、`tools/imgtool.py` |
| 3 Integrator | `8dc07c9` | 迭代式 `PathIntegrator`、栈上 `BSDF`、`HitRecord::mat` 改为裸指针、基于 throughput 的 RR、lobe 按能量选择（M4）、Metal fuzz（M8）、计数器、benchmark |
| 4 Debug/Quality | `3007f47` | Debug AOV（16 种）、Welford 方差、guide 与颜色使用相同样本、variance-guided 降噪 |
| 5 Presentation | 本报告所在 commit | CMake、渲染脚本、重新渲染的图片、前后对比、README、`docs/architecture.md`、本报告 |

Audit 中的问题，本轮已处理：F1–F14、H1、H6-1、M1、M2、M3、M4、M6（NaN/Inf 丢弃并计数）、M7（stratum 按像素打乱顺序）、M8、M9、M10、L6（场景可复现）、L7（死代码）、N2、N7、P-1、P-2、P-3、P-4、P-8（降噪改为并行）、P-9、§7 第 1–4 项、§10（Integrator 拆分）、§11、§12（除单像素路径日志外）。

## 2. Correctness

`--test`：179 / 179 通过，耗时 9.1 s。

**PDF 归一化**（确定性求积，网格在 lobe 附近加密）

| 分布 | 结果 |
|---|---|
| Cosine PDF ∫pdf dω | 1.00000；χ²：p = 0.96 |
| Sphere PDF ∫pdf dω | 1.00001（修复 F2 之前为 4）；χ²：p = 0.34 |
| GGX VNDF ∫pdf dω vs 1 − P(无效样本)，r ∈ {0.05, 0.1, 0.25, 0.5, 1} × cos θv ∈ {1, 0.71, 0.1} | 15 组全部一致，例如 r=1、cos=1 时 0.49995 vs 0.49921；r=0.05、cos=0.1 时 0.99984 vs 0.99984 |
| GGX VNDF χ²（含无效样本质量） | 6 组，p = 0.12 – 0.79 |
| 完整 PBR 混合分布 `BSDF::sample` vs `BSDF::pdf` χ² | 12 组，p = 0.017 – 0.91 |
| Environment PDF（`suburban_garden_2k.hdr`，每个 texel 2×2 采样点的确定性积分） | **1.000000**；采样 χ²：p = 0.96 |
| Quad / Sphere 光源 ∫pdf dω | 1.00038 ± 0.003 / 0.99598 ± 0.006；球形光源 E[1/pdf] = 0.15571（解析值 0.15571） |
| 光源选择混合分布 E_mix[p_k / p_mix] | env 1.00043 ± 0.0014，quad 1.00042 ± 0.0008，sphere 0.99472 ± 0.0035 |

**白炉测试**（白色 base，E[f·cos/pdf] 与确定性求积两种方法互相校验）

| roughness | cos θv | 金属，旧 Karis G | 金属，新 G2 | 精确 Smith 参考值 | 电介质，旧 | 电介质，新 |
|---|---|---|---|---|---|---|
| 0.05 | 1.0 | 0.9999 | 1.0000 | 1.0000 | 0.9999 | 0.9997 |
| 0.05 | 0.71 | 0.8948 | 1.0000 | 1.0000 | 0.9960 | 0.9997 |
| 0.05 | 0.1 | **0.1990** | **0.9997** | 0.9997 | **1.0603** | 0.9994 |
| 0.1 | 0.1 | 0.1822 | 0.9941 | 0.9941 | 1.0473 | 0.9997 |
| 0.25 | 0.1 | 0.1961 | 0.8968 | 0.8968 | 1.0190 | 0.9995 |
| 0.5 | 1.0 | 0.8598 | 0.9158 | 0.9158 | 0.9943 | 0.9999 |
| 1.0 | 1.0 | 0.3068 | 0.3069 | 0.3069 | 0.9722 | 1.0000 |
| 1.0 | 0.1 | 0.5576 | 0.7602 | 0.7602 | 0.9751 | 0.9999 |

Lambert = 1.000000；各向同性相位函数（albedo 0.7）= 0.700000。PBR BRDF 互易性的最大相对误差为 0。

**MIS / 积分器**
- 多线程确定性：1 / 3 / 全部线程的结果逐位相同。
- RR 无偏：`cornell_small` 开 / 关 RR 的均值为 0.15727 / 0.15717（差值 0.00010 ± 0.00069），`volume_small` 为 0.15322 / 0.15325；开启 RR 后每条路径的顶点数从 5.44 降到 2.68。
- 白炉渲染场景（均匀环境光，解析值为 1）：`furnace_lambert` 均值 0.99921，`furnace_volume` 0.99928。修复 M2 之前两者分别为 1.0198 / 1.0165：空光源列表仍然分到 5% 的 NEE 样本，采样方向固定为 (1,0,0)，却按环境光的 pdf 计算权重。

## 3. Rendering Changes

所有展示图都已重新渲染（`tools/render_showcase.ps1`，seed 1），前后对比图放在 `docs/images/comparison/`。

| 场景 | 变化 | 原因 |
|---|---|---|
| `cornell_smoke` | 体积明显变亮，白色烟雾块变得可见 | F2 + F3（能量 ×16） |
| `pbr_benchmark`、`pbr_normal_map_test` | 场景第一次被面积光照亮 | F7 |
| 所有 PBR 金属 | 掠射角边缘恢复应有的亮度，例如 r=0.05 时 0.20 → 1.0 | H1 |
| 所有 PBR 电介质 | 掠射角不再增能 | M3 |
| `readme_showcase` 金色球、`pbr_ibl_test` | 法线贴图第一次生效（金色球呈现锤纹） | H6-1 |
| 所有 LDR 纹理 | 颜色不再偏暗、偏饱和；法线贴图不再整体倾斜 38.7° | F1 |
| `readme_showcase`、`pbr_ibl_test`、`environment_small` | 面积光下的阴影噪声显著减少 | M1 |
| `cornell_box` | 玻璃球不再占用一半的 NEE 样本 | M10 |

`Metal049A`（展示场景的主球）的 roughness 贴图确认是真实的 roughness，并非 glossiness，不做转换，理由见 `ASSET_REVIEW.md`。

光源选择在同样 spp 下的效果（`readme_showcase`，640×360，64 spp，与当前代码 4096 spp 的参考图比较）：

| 区域 | 旧权重（`5cc2bb4`） | 新权重（`d7f68db`） |
|---|---|---|
| 面积光下的地面，relMSE（剔除 0.1% 离群值），seed 11/12/13 | 0.0353 / 0.0345 / 0.0350 | 0.0072 / 0.0075 / 0.0075 |
| 整幅图，relMSE（剔除 0.1% 离群值） | 0.383 / 0.252 / 0.208 | 0.381 / 0.243 / 0.203 |

整幅图未剔除离群值的 relMSE 在两个版本中都会被少数 firefly 主导，不同 seed 之间相差可达 20×（0.7–21），不能用来比较，所以上表只报告剔除离群值后的结果。

## 4. Performance

`pbr_benchmark`，800×450，400 spp，max depth 20，seed 1，Release x64，MSVC 19.38，同一台机器：

| 版本 | 1 线程 | 20 线程 |
|---|---:|---:|
| 原始代码 + Audit 修复（`74c3601`，`Camera::Render`，`std::execution::par`） | 98.33 s | 47.19 s |
| 积分器重构前（`2a80474`，新 harness，新 RNG） | 74.66 s | 46.77 s |
| 积分器重构后（`8dc07c9`，64 spp 测试） | 8.53 s（64 spp） | 0.515 s（64 spp） |
| **最终版本**（`3007f47` 之后） | **54.73 s** | **3.42 s** |

- 20 线程下，同一 harness 的墙钟时间从 46.77 s 降到 3.42 s（13.7×），与原始代码相比为 13.8×。
- 单线程提升 1.36×，主要来自每次反弹少一条射线、去掉堆分配、纹理只采样一次。
- AOV / guide / 方差的逐样本累加带来约 5% 的开销（64 spp 单线程 8.53 s → 8.93 s）。
- 每条相机路径：3.48 条射线（1 条主射线 + 0.558 条延续射线 + 1.922 条阴影射线），平均 0.563 个散射顶点；每条射线访问 5.36 个 BVH 节点、测试 2.92 个图元。
- 堆分配：20000 条相机路径中 0 次（`--test alloc`）；完整渲染共 22 次（缓冲区和线程）。
- 旧代码渲染 `cornell_smoke` 600×600、961 spp 用时 382 s；新代码 1000 spp 用时 19 s。

## 5. Parallel Scaling

最终版本（`--bench --threads-list 1,2,4,8,16,20`，配置同上）：

| Threads | Time (s) | Speedup | Efficiency |
|---:|---:|---:|---:|
| 1 | 54.731 | 1.00× | 100.0% |
| 2 | 27.562 | 1.99× | 99.3% |
| 4 | 14.394 | 3.80× | 95.1% |
| 8 | 7.794 | 7.02× | 87.8% |
| 16 | 4.108 | 13.32× | 83.3% |
| 20 | 3.420 | 16.01× | 80.0% |

重构前（`2a80474`，64 spp）：1 线程 11.77 s，2 线程 1.24×，4 线程 1.44×，8 线程 1.44×，16 线程 1.48×，20 线程 1.50×。4 线程之后几乎不再加速，这与 Audit 的判断一致：瓶颈是 `make_shared` 和 `shared_ptr<Material>` 的原子引用计数竞争，而不是调度。16 → 20 线程的效率下降，部分原因是 20 个线程中有 12 个 E-core。

## 6. Architecture

- **`Camera`**：只负责相机模型和渲染循环（tile、样本、累加、输出），不再包含积分器。
- **`PathIntegrator`**（`Integrator.h`）：迭代式积分器，保留了 Audit 验证过的 MIS 结构（N_L 次光源采样 + 1 次 BSDF 采样，multi-sample power heuristic，delta 之后 emission 权重为 1）。改动在于 BSDF 样本直接作为路径的延续；另外按散射次数记录 `PathSample`。
- **`BSDF`**（`BSDF.h`）：值类型，`Material::GetBSDF()` 在每次命中时构建一次；sample / eval / pdf / cosine 都在这里实现。删除了 `Scattered_Record`、`Mixture_PDF`、`Hittable_PDF`、`Environment_PDF`，以及 `Material::PDF` / `ShadingNormal` / `BSDFSamplingPreference`。
- **`Microfacet.h`**：GGX 数学的唯一实现，采样器和 BRDF 共用，不会再各自漂移。
- **`LightSampler`**：取代 `Hittable::sampling_power_estimate`；无法采样的对象在 build 时给出警告并被忽略。
- **`HitRecord::mat`**：改为 `const Material*`。
- **`Scenes.cpp`**：场景构建与渲染分离，`SceneDesc` + 场景注册表，CLI、测试和 benchmark 共用。
- **`RenderAOV.h`**、**`Stats.h`**、**`Parallel.h`**、**`Sampler.h`**、**`ImageIO.h`**：新增的基础设施。

详见 `docs/architecture.md`。

虚函数没有去掉（Audit §10 / P-10 的建议）：`Material`、`Texture`、`Hittable` 仍是虚接口。在去掉分配和原子操作之后，20 线程效率已达到 80%，目前没有数据表明 virtual 是瓶颈。

## 7. Denoiser

新增内容：
1. guide 与颜色使用同一批样本累加（抖动、景深、运动模糊），并且取自第一个非 delta 顶点；体积的 guide 法线取视线方向。
2. Welford 亮度方差；边缘停止项按 `sqrt(Var_p + Var_q)` 缩放（对称）。第一版只用中心像素的方差，在 16 spp 下丢失了 4% 的能量，改为对称后误差在 0.7% 以内。
3. 方差按权重的平方一起滤波，并做 3×3 预滤波；迭代 4 次。
4. 保守的 albedo demodulation（下限 0.02，没有 guide 时原样保留）。
5. 直接可见的发光体不参与滤波，也不作为邻居。最初的版本中 Cornell 盒的灯光会渗进天花板，导致 `volume_small` 的误差比 raw 还大。
6. glossy 像素（`p_spec · (1 − roughness)²`）降低滤波强度。
7. 按行并行。

参数（σ = 2，4 次迭代）是用 `--denoise-eval --sweep` 在 4 个测试场景、16 spp 和 64 spp 下选出来的。

| 场景 | 16 spp：raw / 旧 / 新（relMSE） | 64 spp：raw / 旧 / 新 |
|---|---|---|
| cornell_small | 0.1361 / 0.1306 / 0.0226 | 0.0259 / 0.0216 / 0.0053 |
| normal_map_small | 0.0314 / 0.0265 / 0.0173 | 0.0109 / 0.0095 / 0.0058 |
| environment_small | 0.7126 / 0.7047 / 0.0625 | 0.0748 / 0.0824 / 0.0537 |
| volume_small | 0.0077 / 0.0040 / 0.0027 | 0.0018 / 0.0009 / 0.0008 |

2048 spp 时，新滤波器相对 raw 的改动（relMSE 0.00008）比旧滤波器（0.00084）小 11 倍，说明它会随收敛程度自动退出。

## 8. Regression Tests

`--regress`（seed 20260930，约 1 s；参考图为 1024 spp，生成约 20 s）：

| 场景 | 验证内容 | 判定条件 |
|---|---|---|
| `furnace_lambert` | 均匀环境光下的白色 Lambert 凸体 | 均值 = 1 ± 0.5% |
| `furnace_volume` | albedo = 1 的体积（F2 / F3） | 均值 = 1 ± 0.5% |
| `furnace_pbr` | 白色金属 / 电介质的 roughness 扫描（H1 / M3） | relMSE ≤ 1.5× 期望值，均值偏差 ≤ max(1%, 3× 期望值) |
| `cornell_small` | 面积光 NEE + MIS + 漫反射互反射 | 同上 |
| `volume_small` | 体积中的 NEE | 同上 |
| `normal_map_small` | 法线贴图（三角形 + `Sphere`） | 同上 |
| `environment_small` | HDRI 重要性采样 + 面积光 + 光源选择 | 同上 |

最终运行结果（在 clean rebuild 之后）：

| 场景 | relMSE | 期望 relMSE | 均值 | 参考均值 | 结果 |
|---|---:|---:|---:|---:|---|
| furnace_lambert | 0.002028 | 解析值 1 | 0.99921 | 1 | PASS |
| furnace_volume | 0.002970 | 解析值 1 | 0.99928 | 1 | PASS |
| furnace_pbr | 0.001494 | 0.001494 | 0.96343 | 0.96385 | PASS |
| cornell_small | 0.025426 | 0.025426 | 0.15697 | 0.15711 | PASS |
| volume_small | 0.001933 | 0.001933 | 0.15309 | 0.15296 | PASS |
| normal_map_small | 0.011707 | 0.011707 | 1.34800 | 1.35015 | PASS |
| environment_small | 0.071405 | 0.071405 | 0.81905 | 0.82702 | PASS |

参考图在 `fa6f6c0` 首次生成，之后又更新过三次，每次都是有意的，原因写在对应的 commit message 中：Smith G（`5cc2bb4`）、sphere 切线 + 光源选择（`2a80474`）、`normal_map_small` 改用强法线贴图（`3007f47`）。积分器重构（`8dc07c9`）是在**参考图保持不变**的情况下通过回归的，证明重构没有引入偏差；之后才连同 `3007f47` 一起更新。

单元测试共 179 项，分组如下：rng、pdf、environment、light、lightsampler、tangent、bsdf、texture、determinism、roulette、alloc、denoise、aov（`--test <组名>` 可以只跑一组）。

## 9. Documentation

- `README.md`：按实际代码重写。修正了 Audit §2.4 指出的 10 处不一致，删除了本机绝对路径，补充了 Deterministic / Testing / AOV / Denoiser / Benchmark / Output / Build / Limitations 等章节，benchmark 数字全部重新测量。
- `docs/architecture.md`：模块关系、积分器流程、测试保护的不变量。
- `ASSET_REVIEW.md`：Metal049A 等资产的审查记录。
- `docs/images/`：7 张重新渲染的展示图 + 7 张前后对比图。
- `tools/build.ps1`、`tools/render_showcase.ps1`、`tools/imgtool.py`，以及可以正常使用的 `CMakeLists.txt`（原来是空文件）。

## 10. Remaining Problems

- **H4 射线偏移**：三角形求交会把 `p` 沿面法线偏移 0.001（绝对值）；三角网格玻璃会自相交；`t_min = 0.001` 与场景尺度相关。**NOT FIXED**。
- **H6-2/3/4**：切线按面计算，没有 handedness，没有 shadow terminator 处理。**NOT FIXED**。
- **M5 OBJ 语义**：`map_bump` 被当作法线贴图，UV 被 clamp，`map_d` 被忽略，标量 `Pr` / `Pm` 被忽略。**NOT FIXED**。
- **单次散射 GGX**：粗糙金属偏暗（r=1 正入射时 0.31），没有 Kulla-Conty 补偿。
- **BVH**：object median 分割，递归、虚函数遍历。当前场景每条射线约访问 5.4 个节点，网格场景尚未测量。
- **降噪器**：极低 spp 下大面积平坦区域有 A-Trous 斑块；镜面反射内容只能靠 glossiness 启发式保护。
- **展示场景**：雾球被玻璃外壳包住，体积内的 NEE 总是被外壳挡住（Audit N4，属于场景设计问题）。
- **Low**：L1（Dielectric 出射时的 Schlick）、L2（三角形 det 使用绝对阈值）、L3（空 BVH / 叶子重复测试）、L4（环境贴图半个 texel 的错位）、L5（逐通道 Reinhard）。
- **Debug**：没有单像素路径日志（`--debug-pixel`），预览窗口不能切换 AOV。
- **丢弃的非有限样本**：会计数并报告，但直接丢弃会略微压暗对应像素；本次所有渲染中没有出现。
- **光源选择**：估计值不考虑可见性，也不考虑着色点的余弦；10% 的防御性混合保证了无偏，但不是最优的。
- **缺失资产**：`dragon.obj`、`sponza.obj`、`sphere.obj` 不在仓库中，相应场景无法完整渲染。
- **NOT VERIFIED**：CMake 构建只在 VS 2022 生成器下验证过；VS 2026（v145）没有条件验证；非 Windows 平台不支持。

## 11. Recommended Next Step

1. **SAH + 扁平 BVH + mesh 索引布局**（Audit P1-4）：先用 `--bench` 的节点访问计数在网格场景（teapot / 补充 Sponza）上建立基线，再对比。
2. **射线偏移鲁棒化**（H4）：去掉三角形求交内部的偏移，在生成新射线时沿 `geo_n` 按尺度偏移，并增加 `glass_mesh` 回归场景。
3. **法线贴图补全**（H6-2/3）：按顶点累加切线并保存 handedness；配合 glTF 的 `NormalTangentMirrorTest` 或手工构造的镜像 UV 面板来验证。
4. **单像素路径日志 `--debug-pixel x y`**：直接复用 `PathSample` / 积分器的状态，回答“这个像素为什么是黑的”。
5. **Kulla-Conty 多次散射补偿**：用现有的白炉测试验证粗糙金属的反照率 → 1。
