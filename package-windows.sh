#!/usr/bin/env bash
set -euo pipefail

if [[ ${MSYSTEM:-} != UCRT64 ]]; then
    printf 'Run this script in the MSYS2 UCRT64 shell.\n' >&2
    exit 1
fi

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(cygpath -au "${HS_STV_WINDOWS_BUILD_DIR:-$project_dir/build-windows}")"
package_dir="$(mktemp -d "$build_dir/package.XXXXXX")"
cmake --install "$build_dir" --prefix "$package_dir"
bin="$package_dir/bin"
windeployqt6 --release --no-translations --include-plugins qjpeg "$bin/hs-stv.exe"

# windeployqt handles Qt and its plugins. Resolve additional MSYS2 imports,
# including PortAudio and any dependencies of the deployed plugins.
shopt -s globstar nullglob
pending=("$bin/"*.exe "$bin/"**/*.dll)
while (( ${#pending[@]} )); do
    binary="${pending[-1]}"
    unset 'pending[-1]'
    imports="$(objdump -p "$binary" | sed -n 's/^[[:space:]]*DLL Name: \([^[:space:]]*\).*/\1/p')"
    while IFS= read -r dll; do
        if [[ -n $dll && -f "$MINGW_PREFIX/bin/$dll" && ! -f "$bin/$dll" ]]; then
            cp "$MINGW_PREFIX/bin/$dll" "$bin/$dll"
            pending+=("$bin/$dll")
        fi
    done <<< "$imports"
done

cp "$project_dir/LICENSE" "$package_dir/LICENSE"
cp "$project_dir/external/liquid-dsp-1.8.3/LICENSE" "$package_dir/LICENSE-liquid-dsp"
printf 'Windows package: %s\n' "$(cygpath -m "$package_dir")"
