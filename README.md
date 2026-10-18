# Vulkan Engine

[![ClangFormat](https://github.com/BykadorovR/Raytracer/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/BykadorovR/Raytracer/actions/workflows/clang-format-check.yml)

This project is a cross-platform (Windows, Linux, Android) graphic engine utilizing the Vulkan API, offering a comprehensive set of features to facilitate the development of visually compelling applications. The engine includes support for terrain rendering, various lighting models, particle systems, skeletal animation and more.

At its core, Vulkan Engine is powered by [RenderGraph](https://github.com/BykadorovR/RenderGraph), which manages rendering passes, GPU resources and synchronization.

# Structure

- **dev/master** branch contains classic rasterization engine
- **raytracing** branch contains Path Tracer with importance sampling using compute shader
- [samples](samples) set of samples which demonstates how to use the engine with description and videos

# Architecture

[Draw.io diagrams](https://drive.google.com/file/d/1eI9kWqMdEsKQL0aQhGGtAJed9jmwzZk7/view?usp=sharing)

# How to install

## Dependencies

- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)
- C++ compiler with C++20 support
- [Python](https://www.python.org/) 3.12.0 or above to download assets

## Build

- Clone the project
  ```
  git clone -b master --single-branch https://github.com/BykadorovR/VulkanEngine.git
  ```
- Install Python dependencies
  ```
  pip install -r requirements.txt
  ```
- Download assets
  ```
  python download.py
  ```
- Build project
  ```
  mkdir build
  cd build
  cmake ..
  cmake --build .
  ```
