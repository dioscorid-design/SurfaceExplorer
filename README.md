# Surface Explorer 4D
**A high-performance 4D surface visualizer built with Qt6 and modern OpenGL.**

Surface Explorer 4D is an advanced interactive tool designed to visualize, animate, and explore complex mathematical surfaces in 3D and 4D spaces ($x, y, z, p$). Built for researchers, artists, and math enthusiasts, it leverages modern GPU hardware to render real-time parametric geometries with high precision.

## 🚀 Key Features
* **4D Spatial Exploration**: Native support for four-dimensional coordinate systems with dedicated 4D rotation and projection controls.
* **Modern Rendering Engine**: Powered by **OpenGL 4.6 (Core Profile)** and GLSL 460 for maximum performance on modern GPUs.
* **Dynamic GLSL Scripting**: Integrated engine to write custom shaders for procedural textures and surface logic on the fly.
* **Cross-Platform Excellence**: Optimized for **Fedora Linux (Wayland/AMD)** and **Windows 10/11** with full High-DPI scaling support.
* **Audio-Visual Sync**: Dynamic sound synthesis and music integration (FFmpeg powered) mapped to geometric parameters.

## 💻 System Requirements
* **OS**: Linux (Fedora/Ubuntu recommended) or Windows 10/11.
* **Graphics**: GPU with **OpenGL 4.6** support (NVIDIA, AMD, or Intel Arc).
* **Framework**: Qt 6.10.2 or higher.

## 📦 Releases (Alpha)
You don't need to compile the project to try it! Check out the **[Latest Releases](https://github.com/dioscorid-design/SurfaceExplorer/releases)** for portable, standalone binaries:
* **Windows**: Download the `SurfaceExplorer_Windows_Alpha.zip`, extract, and run `SurfaceExplorer.exe`.
* **Linux**: Download the `SurfaceExplorer_Linux_Alpha.zip`, extract, and run `./surface-explorer.sh`.

## 🛠️ Build from Source
To compile Surface Explorer manually, ensure you have **Qt 6.10.2** and **CMake** installed:

```bash
# 1. Clone the repository
git clone [https://github.com/dioscorid-design/SurfaceExplorer.git](https://github.com/dioscorid-design/SurfaceExplorer.git)
cd SurfaceExplorer

# 2. Configure and Build
cmake -B build -S .
cmake --build build -j$(nproc)
