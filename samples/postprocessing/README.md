# Postprocessing

Gamma, tone-mapping and bloom based on blur.

![](https://media.giphy.com/media/JJ4g8ol6s28fQfZlNn/giphy.gif)

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
