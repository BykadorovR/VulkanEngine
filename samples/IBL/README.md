# IBL

This sample demonstrates IBL, based on skybox color values. Used model is rendered as PBR.

![](https://media.giphy.com/media/WK8R12t5jQfdinyTT6/giphy.gif)

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
