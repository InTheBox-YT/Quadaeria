#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(pwd)"

install_deps() {
    if command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm base-devel cmake ninja git pkgconf \
            libx11 libxrandr libxinerama libxcursor libxi libxkbcommon \
            wayland wayland-protocols libglvnd alsa-lib libpulse
    elif command -v apt-get >/dev/null; then
        sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev \
            libwayland-dev wayland-protocols libgl-dev libasound2-dev
    elif command -v dnf >/dev/null; then
        sudo dnf install -y gcc-c++ cmake ninja-build git pkgconf-pkg-config \
            libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
            libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libGL-devel alsa-lib-devel
    else
        echo "Unknown package manager - install CMake, a C++20 compiler and X11/Wayland/GL/ALSA headers yourself."
    fi
}

ACTION=""
DEPS=1
for a in "$@"; do
    case "$a" in
        --no-deps) DEPS=0 ;;
        run|install) ACTION="$a" ;;
        *) echo "unknown option: $a"; exit 1 ;;
    esac
done
[[ $DEPS == 1 ]] && install_deps

GEN=""
command -v ninja >/dev/null && GEN="-G Ninja"
cmake -S . -B build $GEN -DCMAKE_BUILD_TYPE=Release -DQC_NATIVE=ON -DQC_LTO=ON
cmake --build build --config Release -j"$(nproc)"

mkdir -p bin
cp -f build/quadaeria bin/
echo
echo "Built:  ./bin/quadaeria"

if [[ "$ACTION" == "install" ]]; then
    APPS="$HOME/.local/share/applications"
    mkdir -p "$APPS"
    cat > "$APPS/quadaeria.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Quadaeria
Comment=Half-size-block sandbox with real physics
Exec=$ROOT/bin/quadaeria
Path=$ROOT/bin
Terminal=false
Categories=Game;
EOF
    echo "Added Quadaeria to your application menu."
fi
if [[ "$ACTION" == "run" ]]; then exec ./play.sh; fi
