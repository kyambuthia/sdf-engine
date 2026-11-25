# SDF Engine (C++20 + SDL3 + OpenGL ES 3 / WebGL2)

A small, educational 3D **Signed Distance Field (SDF)** raymarching engine written in C++20 with **SDL3** and **GLSL ES 3.00** shaders. Designed to compile to **WebAssembly + WebGL2** via Emscripten as well as native desktop/mobile targets.

## Features
- **SDL3** windowing, input (mouse orbit, scroll zoom, touch drag, keyboard shortcuts)
- **Fullscreen triangle** raymarching pass (`gl_VertexID`, no VBO required)
- **4 interactive SDF scenes**:
  1. **Smooth Blending** (`opSmoothUnion` / `smin` metaball-style clay)
  2. **CSG Booleans** (subtraction & intersection)
  3. **Infinite Domain Repetition** (`mod` tiling + twist)
  4. **Procedural Pillars & Floating Capsule**
- Soft shadows, ambient occlusion, finite-difference normals, Fresnel rim lighting

## Build for Web (Emscripten)
```bash
source /opt/emsdk/emsdk_env.sh
emcmake cmake -B build-web -G Ninja
cmake --build build-web
python3 -m http.server 8080 --directory build-web
```
Open `http://localhost:8080/sdf_engine.html`.

## Controls
- **Left-drag / Touch-drag**: Orbit camera
- **Scroll / `+` / `-`**: Zoom
- **`1`–`4`**: Switch SDF scene
- **`Space`**: Pause/resume animation
- **`R`**: Reset camera
