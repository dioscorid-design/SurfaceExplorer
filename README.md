# Surface Explorer 4D
**A high-performance 4D surface visualizer built with Qt6 and OpenGL.**

Surface Explorer 4D is an interactive tool designed to visualize and explore complex mathematical surfaces in 3D and 4D spaces. Developed on **Fedora Linux** and optimized for **AMD GPUs**, it leverages the power of modern OpenGL to render real-time parametric geometries.

## 🚀 Features
* **4D Visualization**: Explore surfaces with four spatial dimensions (x, y, z, p).
* **Real-time Rendering**: Powered by **OpenGL 3.3** for high-speed graphics.
* **Dynamic Scripting**: Integrated GLSL scripting engine for custom textures and surface logic.
* **Cross-Platform Core**: Built using **Qt 6.10.2**, ensuring a portable codebase.
* **Audio Integration**: Dynamic sound synthesis mapped to geometric parameters.

## 🛠️ Requirements
* **OS**: Linux (Optimized for Fedora), Windows, or macOS.
* **Compiler**: C++17 compatible compiler (GCC/Clang/MSVC).
* **Framework**: Qt 6.10.2 or higher.
* **Graphics**: GPU with OpenGL 3.3 support.

## ⚠️ Current Status: Work in Progress
This project is currently developed and optimized for **Linux (Fedora)** using **AMD GPUs**. 

* **Linux**: Fully supported and tested (Wayland/XCB).
* **Windows/macOS**: Code is written in portable Qt6/C++, but binary deployment and hardware-specific OpenGL testing are still in progress.

## 📦 How to Build
To compile **Surface Explorer** from source on Linux, ensure you have **Qt 6.10.2** and **CMake** installed, then run the following commands in your terminal:

```bash
# 1. Clone the repository
git clone [https://github.com/dioscorid-design/SurfaceExplorer.git](https://github.com/dioscorid-design/SurfaceExplorer.git)
cd SurfaceExplorer

# 2. Configure the project
cmake -B build -S .

# 3. Build the project
cmake --build build -j$(nproc)
