# Shape primitives

This sample demonstrates different lighting models: simple coloring/texturing, Blinn-Phong, PBR using simple primitives: cube and sphere. Direct and point lights are used as light sources.

![](https://media.giphy.com/media/WNxu1YsdBWca4A5jKn/giphy.gif)
![](https://media.giphy.com/media/Hw72BHfihIqkTKr9z4/giphy.gif)

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
