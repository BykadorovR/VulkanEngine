# Shadow

Simple implementation of shadows for point and directional lights. Only objects with PBR or Phong materials can be affected by shadows. Objects with any material can cast shadows.

![](https://media.giphy.com/media/v1.Y2lkPTc5MGI3NjExaTlyeDNkZzZiYWN6Nmt5ZmV1MW9uMGg2eHgxN21lYzk1dm9xYzl0NyZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/psoypsGR4sR58uZs55/giphy-downsized-large.gif)

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
