# Terrain

GPU generated terrain based on heightmap texture. Samples shows different material settings.

![](https://media.giphy.com/media/9kFtsK5QWa7Y8v3NEJ/giphy.gif)

# How to install

## Build

- Download assets by running `python download.py` from the repository root. Skip this step if it was already done during engine setup.
- Build project
  ```
  mkdir build
  cd build
  cmake -DCMAKE_PREFIX_PATH=../../cmake ..
  cmake --build .
  ```
