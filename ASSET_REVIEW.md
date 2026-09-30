# Asset Review

Scope: texture sets used by the showcase / validation scenes. Statistics are the raw encoded
values (no color-space decoding) measured with `PathTracer-CPP.exe --image-stats <file>`.

## Metal049A (`PathTracer-CPP/images/Metal1/`)

| Map | Mean | p5 / p50 / p95 | Min / Max |
|---|---|---|---|
| `Metal049A_2K-JPG_Roughness.jpg` | 0.034 | 0.024 / 0.031 / 0.047 | 0.008 / 0.078 |
| `Metal049A_2K-JPG_Metalness.jpg` | 1.000 | 1.0 / 1.0 / 1.0 | 0.976 / 1.0 |
| `Metal049A_2K-JPG_Color.jpg` (sRGB encoded) | (0.987, 0.984, 0.965) | | |
| `Metal049A_2K-JPG_NormalGL.jpg` | (0.501, 0.499, 0.999) | | |

**Question from the audit:** the roughness map is almost black (mean 0.034). Is it really
roughness, or glossiness / smoothness, an inverted map, the wrong channel, or a color-space
problem?

**Evidence**

1. The folder only contains `Color`, `Metalness`, `NormalGL`, `Roughness` (no `.mtlx` / `.usdc`
   for this set). The file suffix is `_Roughness`, the ambientCG naming for the
   metallic-roughness workflow. ambientCG does not ship glossiness maps.
2. The sibling sets from the same source (`Metal_Gold/Metal048C_1K-JPG.mtlx`,
   `ChristmasTreeOrnament019_1K-JPG.mtlx`) bind `*_Roughness.jpg` to OpenPBR
   `specular_roughness` without any inversion, so the convention is roughness, not smoothness.
3. The public asset page (ambientCG, "Metal 049 A") tags the material *Clean, Metal, Silver,
   Smooth*, i.e. a polished metal. A near-zero roughness is consistent with that; an inverted
   (glossiness) interpretation would give roughness ~0.97, a fully diffuse-looking metal,
   contradicting the tags.
4. All three channels are identical (grayscale), so the channel choice (R) is not the issue.
5. Color-space: since fix F1 the map is read as raw encoded values (Linear). With the old
   double decoding the value would have been even lower (0.034^2.2 ~ 0.0006), so the dark
   values are not an artifact of the current decoding.
6. The normal map is essentially flat (mean tangent-space normal (0.001, -0.002, 1.000)),
   again consistent with a smooth, clean surface.

**Conclusion:** the map is a genuine, very low roughness map. It is **not** converted or
inverted. The material is intentionally close to a mirror.

**Renderer behavior to be aware of:** `PBR_Material` clamps roughness to `[0.05, 1.0]`
(`sample_scalar(..., 0.05, 1.0)`), so every texel of this map (max 0.078) renders at an
effective roughness of ~0.05, i.e. alpha = 0.0025. The spatial variation of the map is therefore
almost entirely removed. This is a renderer limitation (a floor to keep the GGX lobe numerically
tractable), not an asset problem.

## Metal048C (`PathTracer-CPP/images/Metal_Gold/`)

| Map | Mean | p5 / p50 / p95 |
|---|---|---|
| `Metal048C_1K-JPG_Roughness.jpg` | 0.281 | 0.204 / 0.271 / 0.388 |
| `Metal048C_1K-JPG_Metalness.jpg` | 0.991 | 0.961 / 1.0 / 1.0 |

The `.mtlx` binds `Color` as `srgb_texture` and the other maps as raw data, which matches the
scene code (`color_space::SRGB` for color, `Linear` for data). No issue found. The set also
ships `NormalDX`; the scenes use `NormalGL` with the OpenGL convention, which is correct.

## ChristmasTreeOrnament019 (`PathTracer-CPP/images/ChristmasTreeOrnament019/`)

Roughness mean 0.144 (p5 0.10, p95 0.22). Used only by `pbr_test`, which additionally requires
`Model/sphere.obj`; that model is **not in the repository**, so this scene cannot be rendered
from a clean checkout.

## Missing assets referenced by scenes

| Scene | Missing file |
|---|---|
| `obj_test` (11) | `Model/dragon.obj` |
| `sponza` (13) | `Model/sponza.obj` |
| `pbr_test` (14) | `Model/sphere.obj` |

These scenes print a warning and render without the model.
