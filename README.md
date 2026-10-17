# Quadaeria

voxel sandbox game im working on. everything is built out of a quater of a minecraft block for example and and theres also dynamic physics and stuff

not released yet, still very much in progress.

## building

linux only for now. you need cmake, a c++20 compiler and the X11/Wayland/GL/ALSA dev packages and stuff
by extracting the zip, rightclick when inside it and open in termal and then:

```
./build.sh run
```

or by hand

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/quadaeria
```


## controls

- wasd / mouse - move, look
- space - jump, double tap to fly
- left / right click - break / place
- middle click - pick block
- r - full block or single quad
- e - inventory
- f4 - spectator
- f3 - debug
- esc - pause
