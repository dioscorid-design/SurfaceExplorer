# Surface Explorer 1.1
**A high-performance multi-backend 4D surface visualizer built with Qt6 and Qt RHI.**

Surface Explorer 1.1 is built on the Qt RHI architecture and adds two major rendering and visualization capabilities: a **Ray Marching engine** for implicit surfaces and signed-distance fields, and a **Geodesic Flow** solver for intrinsic geometry on curved manifolds. By relying on the **Qt Rendering Hardware Interface (RHI)**, the application remains hardware-agnostic, automatically leveraging the most efficient graphics API available on your system: **Vulkan**, **Metal**, **Direct3D**, or **OpenGL**. This version is designed for high-precision visualization of complex mathematical surfaces in 4D space ($x, y, z, p$), providing a seamless bridge between abstract geometry and real-time GPU performance.

## 🚀 Key Features
* **Ray Marching Engine**: Real-time rendering of implicit surfaces and signed-distance fields directly on the GPU, with support for procedural displacement, transparency, and true multi-layer field surfaces.
* **Geodesic Flow**: Intrinsic geometry on curved manifolds — integrate geodesics from initial conditions and explore metric-driven surfaces (S³, H²×R, SL(2,R), and more) via the Equations panel.
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

## 📦 Releases
You don't need to compile the project to try it! Check out the **[Latest Releases](https://github.com/dioscorid-design/SurfaceExplorer/releases)** for portable, standalone binaries:

* **macOS**: Download `SurfaceExplorer.dmg`, open it, and drag the app to your Applications folder. The app is signed with an Apple Developer ID and notarized, so it opens normally on first launch — no Gatekeeper workaround needed.
* **Windows**: Download `SurfaceExplorer-win64.zip`, extract, and run `SurfaceExplorer.exe`.
* **Linux**: Download `SurfaceExplorer-*-linux-x86_64.AppImage` — a self-contained binary that bundles Qt, so it runs on any x86_64 distribution with no installation. Make it executable and launch it:
  ```bash
  chmod +x SurfaceExplorer-*-linux-x86_64.AppImage
  ./SurfaceExplorer-*-linux-x86_64.AppImage
  ```
  To add it to your applications menu (icon + launcher), download `install-linux.sh` into the same folder and run it:
  ```bash
  chmod +x install-linux.sh
  ./install-linux.sh            # integrates into the menu; re-run to update, --uninstall to remove
  ```

## 🛠️ Build from Source
To compile Surface Explorer manually, ensure you have **Qt 6.10.2** and **CMake** installed:

```bash
# 1. Clone the repository
git clone https://github.com/dioscorid-design/SurfaceExplorer.git
cd SurfaceExplorer

# 2. Configure and Build
cmake -B build -S .
cmake --build build -j$(nproc)
```
