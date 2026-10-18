#!/bin/sh
# Cross-compile the Release x86 d3d9.dll with llvm-mingw and Meson,
# then pack Portal1VR-Windows-x86.zip (or the path given as $1).
set -eu
cd "$(dirname "$0")"
root=$(pwd)
build_dir=$root/build-mingw
output=${1:-$root/Portal1VR-Windows-x86.zip}

if ! command -v meson >/dev/null 2>&1; then
    echo 'meson is not on PATH.' >&2
    exit 1
fi
if ! command -v ninja >/dev/null 2>&1; then
    echo 'ninja is not on PATH.' >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo 'python3 is not on PATH.' >&2
    exit 1
fi

write_path_cross_file() {
    mkdir -p "$build_dir"
    cat > "$build_dir/cross.txt" << 'EOF'
[binaries]
c = 'i686-w64-mingw32-gcc'
cpp = 'i686-w64-mingw32-g++'
ar = 'i686-w64-mingw32-ar'
strip = 'i686-w64-mingw32-strip'
windres = 'i686-w64-mingw32-windres'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'x86'
cpu = 'i686'
endian = 'little'
EOF
    echo "$build_dir/cross.txt"
}

if [ -n "${PORTAL1VR_CROSS_FILE:-}" ]; then
    cross=$PORTAL1VR_CROSS_FILE
elif [ -f "$root/mingw-cross.txt" ]; then
    cpp=$(sed -n "s/^cpp = '\\([^']*\\)'/\\1/p" "$root/mingw-cross.txt" | head -n 1)
    if [ -n "$cpp" ] && [ -x "$cpp" ]; then
        cross=$root/mingw-cross.txt
    elif command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
        cross=$(write_path_cross_file)
    else
        echo "mingw-cross.txt points at a missing compiler, and i686-w64-mingw32-g++ is not on PATH." >&2
        exit 1
    fi
elif command -v i686-w64-mingw32-g++ >/dev/null 2>&1; then
    cross=$(write_path_cross_file)
else
    echo 'Need mingw-cross.txt or i686-w64-mingw32-g++ on PATH.' >&2
    exit 1
fi

# Portal loads d3d9.dll only. D3D10/11 fail to build on current llvm-mingw
# and are not part of the release.
meson_setup() {
    meson setup "$@" \
        --cross-file "$cross" \
        --buildtype release \
        -Denable_dxgi=false \
        -Denable_d3d10=false \
        -Denable_d3d11=false \
        "$build_dir" "$root/dxvk"
}
if [ -f "$build_dir/build.ninja" ]; then
    meson_setup --reconfigure
else
    meson_setup
fi
ninja -C "$build_dir" src/d3d9/d3d9.dll
mkdir -p "$root/Release"
cp "$build_dir/src/d3d9/d3d9.dll" "$root/Release/d3d9.dll"
python3 "$root/package-release.py" --output "$output"
echo "Built $root/Release/d3d9.dll"
echo "Packed $output"
