# Third-party code and asset inventory

This inventory records repository evidence. It does not relicense assets or establish provenance from filenames alone.

| Content | Evidence / status |
| --- | --- |
| `PathTracer-CPP/external/tiny_obj_loader.h` | MIT notice embedded; embedded fast_float retains its own notices. |
| `PathTracer-CPP/external/stb_image.h` | Embedded MIT / public-domain terms. |
| Tutorial foundation | README credits [Ray Tracing in One Weekend](https://raytracing.github.io/). |
| `images/HDR/suburban_garden_2k.hdr` | Acquisition source/license record absent. Name resembles the [Poly Haven asset](https://polyhaven.com/a/suburban_garden); [Poly Haven license](https://polyhaven.com/license) is CC0, but matching provenance of the committed file needs confirmation. |
| `images/Metal1`, `images/Metal_Gold`, `images/ChristmasTreeOrnament019`, corresponding `Model/Obj_PBRTest/textures` | Asset-pack names are preserved; acquisition and license records absent. Confirm exact provider/download before claiming CC0 or MIT. |
| `images/earthmap.jpg` | Used by the tutorial-style image texture scene; original source/license not recorded. |
| `Model/textures` (Sponza), `Model/sponza.mtl` | Sponza-named resource set. No Sponza mesh or asset license bundled; confirm original source and redistribution terms. |
| `Model/default.mtl`, `Model/default.png`, `Model/teapot.obj`, `Model/Obj_PBRTest/Sphere.obj` | `default.mtl` references Williams graphics data; exact acquisition and asset license not bundled. |

The current cleanup does not add or remove binary assets. The owner should retain download URLs/license records for redistributed assets, or replace uncertain assets with verified alternatives. Root LICENSE remains unchanged.
