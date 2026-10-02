# Third-party notices

## AgX display transform approximation

The AgX matrices and polynomial approximation in `PathTracer-CPP/Color.h` are
adapted from the MIT-licensed three.js r182 shader, `AgXToneMapping` and
`agxDefaultContrastApprox`:

https://github.com/mrdoob/three.js/blob/r182/src/renderers/shaders/ShaderChunk/tonemapping_pars_fragment.glsl.js

Upstream implements a compact approximation based on Filament and Blender's
Rec.2020 AgX transform, with the polynomial from Benjamin Wrensch's minimal
implementation. This project provides the base look with a final sRGB transfer
function. It does not embed Blender's OCIO configuration or LUTs, and its result
is not intended to be an exact match for Blender.

### three.js license

The MIT License

Copyright © 2010-2025 three.js authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
