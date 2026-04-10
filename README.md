# Surface Explorer 4D — Version 2.0
**A high-performance multi-backend 4D surface visualizer built with Qt6 and Qt RHI.**

Surface Explorer 4D 2.0 represents a major architectural evolution. By transitioning from legacy OpenGL to the **Qt Rendering Hardware Interface (RHI)**, the application now offers a hardware-agnostic experience, automatically leveraging the most efficient graphics API available on your system: **Vulkan**, **Metal**, **Direct3D**, or **OpenGL**. This version is designed for high-precision visualization of complex mathematical surfaces in 4D space ($x, y, z, p$), providing a seamless bridge between abstract geometry and real-time GPU performance.

## 🚀 Key Features
* **Qt RHI Engine**: Native support for **Vulkan** (Linux/Windows), **Metal** (macOS), and **Direct3D** (Windows), ensuring smoother performance and future-proof compatibility.
* **4D Spatial Exploration**: Native support for four-dimensional coordinate systems with dedicated controls for hyperspatial rotation (Omega, Phi, Psi) and projection.
* **4D Lighting Models**: Advanced lighting modes specifically designed for hyperspace, including **Directional**, **Observer**, and **Slice** lighting.
* **Dynamic GLSL Scripting**: Integrated engine to write custom shaders for procedural textures or surface logic directly within the built-in editor.
* **Optimized for Fedora & Wayland**: Full integration with modern Linux desktops, including specific optimizations for **AMD GPU** drivers and **Wayland** compositors.
* **Audio-Visual Synthesis**: Real-time synchronization between geometric parameters and a mathematical sound synthesizer or external audio tracks (FFmpeg powered).

## 💻 System Requirements
* **OS**: Linux (Fedora/Ubuntu), Windows 10/11, or macOS 12.0+ (Universal).
* **Graphics**: GPU with support for **Vulkan 1.0+**, **Metal**, **Direct3D 11+**, or **OpenGL 3.3+**.
* **Framework**: Built with **Qt 6.10.2** for maximum stability and High-DPI support.

## 📦 Releases (Alpha)
You don't need to compile the project to try it! Check out the **[Latest Releases](https://github.com/dioscorid-design/SurfaceExplorer/releases)** for portable, standalone binaries:
* **Windows**: Download `SurfaceExplorer_Windows_Alpha.zip`, extract, and run `SurfaceExplorer.exe`.
* **Linux**: Download `SurfaceExplorer_Linux_Alpha.zip`, extract, and run `./surface-explorer.sh`.
* **macOS**: Download `Surface.Explorer.dmg`, open it, and drag the app to your Applications folder.

## 🛠️ Build from Source
To compile Surface Explorer manually, ensure you have **Qt 6.10.2** and **CMake** installed:

```bash
# 1. Clone the repository
git clone [https://github.com/dioscorid-design/SurfaceExplorer.git](https://github.com/dioscorid-design/SurfaceExplorer.git)
cd SurfaceExplorer

# 2. Configure and Build
cmake -B build -S .
cmake --build build -j$(nproc)
