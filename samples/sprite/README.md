# Sprite

This sample demonstrates simple static sprites with and without alpha. To avoid alpha blending artifacts, sorting algorithm (from far to near) is used.

![](https://media.giphy.com/media/0tzpThYXGigkLVKleW/giphy.gif)

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
