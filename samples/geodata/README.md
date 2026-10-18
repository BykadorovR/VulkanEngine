# Geodata

Shows interaction between terrain and playable entity. Terrain height information is used to place entity correctly (center of object is used).

![](https://media.giphy.com/media/J3qycwARqb4CvRzHPz/giphy.gif)

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
