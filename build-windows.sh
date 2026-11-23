#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != UCRT64 ]]; then
    printf 'Run this script in the MSYS2 UCRT64 shell.\n' >&2
    exit 1
fi

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build-windows"
liquid_source="$build_dir/liquid-src"
liquid_prefix="$(cygpath -m "$build_dir/liquid-install")"
liquid_commit=10041f70cebbe3b97887e75bb41e48b73dda1b23
liquid_patch="$project_dir/external/liquid-dsp-1.8.3/windows-timer.patch"

mkdir -p "$build_dir"
if [[ ! -d "$liquid_source/.git" ]]; then
    git clone --depth 1 --branch v1.8.3 https://github.com/jgaeddert/liquid-dsp.git "$liquid_source"
fi
if [[ $(git -C "$liquid_source" rev-parse HEAD) != "$liquid_commit" ]]; then
    printf 'Unexpected liquid-dsp revision in %s\n' "$liquid_source" >&2
    exit 1
fi
if ! git -C "$liquid_source" apply --reverse --check "$liquid_patch" 2>/dev/null; then
    git -C "$liquid_source" apply "$liquid_patch"
fi

cmake -S "$liquid_source" -B "$build_dir/liquid-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$liquid_prefix" \
    -DBUILD_EXAMPLES=OFF -DBUILD_AUTOTESTS=OFF -DBUILD_BENCHMARKS=OFF \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DFIND_FFTW=OFF \
    -DENABLE_LOGGING=OFF -DENABLE_TIMESTAMPS=OFF
cmake --build "$build_dir/liquid-build" --parallel 4
cmake --install "$build_dir/liquid-build"

cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    "-DMM_SSTV_LIQUID_ROOT=$liquid_prefix" \
    "-DCMAKE_PREFIX_PATH=$(cygpath -m "$MINGW_PREFIX")" \
    "-DPython3_EXECUTABLE=$(cygpath -m "$MINGW_PREFIX/bin/python.exe")" "$@"
cmake --build "$build_dir" --parallel 4
