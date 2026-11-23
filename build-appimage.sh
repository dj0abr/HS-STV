#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# One source checkout; independent native x86_64 and aarch64 build directories.
set -Eeuo pipefail
export LC_ALL=C

usage() {
    cat <<'EOF'
Aufruf: ./build-appimage.sh [--arch x86_64|aarch64] [--jobs N]
                           [--allow-newer-glibc]

Baut liquid-dsp und HS-STV komplett neu und erzeugt eine AppImage.
Die Architektur wird automatisch erkannt. PC und ARM werden jeweils nativ
gebaut; --arch prueft die Auswahl und aktiviert keinen Crosscompiler.

Ergebnisse: build-appimage/<Architektur>/output/
Cache:     build-appimage/cache/   (gemeinsame liquid-dsp-Quellen)

Release-Basis: Linux Mint 21.x / Ubuntu 22.04, glibc maximal 2.35.
--allow-newer-glibc erlaubt einen Test auf neueren Systemen. Das Ergebnis
erhaelt dann den Zusatz -development und ist kein Mint-21-Release-Build.

Voraussetzungen (Mint/Ubuntu):
  sudo apt-get install build-essential cmake pkg-config curl ca-certificates file \
    zlib1g-dev libpulse-dev qtbase5-dev qt5-image-formats-plugins \
    qttranslations5-l10n qtwayland5

Es werden keine Systempakete automatisch installiert. Beim ersten Aufruf
werden die festgelegten Quellen und Paketierungswerkzeuge heruntergeladen.
EOF
}
die() { printf 'Fehler: %s\n' "$*" >&2; exit 1; }
normalize_arch() {
    case "$1" in
        x86_64|amd64|pc) printf 'x86_64\n' ;;
        aarch64|arm64) printf 'aarch64\n' ;;
        *) die "Nicht unterstuetzte Architektur: $1 (benoetigt x86_64 oder aarch64)." ;;
    esac
}

requested_arch= jobs= allow_newer=false
while (( $# )); do
    case "$1" in
        --arch) (( $# >= 2 )) || die '--arch benoetigt einen Wert.'; requested_arch="$(normalize_arch "$2")"; shift 2 ;;
        --jobs|-j) (( $# >= 2 )) || die '--jobs benoetigt einen Wert.'; jobs="$2"; shift 2 ;;
        --allow-newer-glibc) allow_newer=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) die "Unbekannte Option: $1 (siehe --help)." ;;
    esac
done
[[ $(uname -s) == Linux ]] || die 'Dieses Skript benoetigt Linux.'
arch="$(normalize_arch "$(uname -m)")"
[[ -z $requested_arch || $requested_arch == "$arch" ]] ||
    die "Ziel $requested_arch passt nicht zum Rechner $arch. Bitte auf dem passenden PC bzw. ARM64-Rechner aufrufen."
if [[ -z $jobs ]]; then
    jobs="$(getconf _NPROCESSORS_ONLN)"
    max_jobs=4; [[ $arch != aarch64 ]] || max_jobs=2
    (( jobs <= max_jobs )) || jobs=$max_jobs
fi
[[ $jobs =~ ^[1-9][0-9]*$ ]] || die '--jobs muss eine positive ganze Zahl sein.'

for command_name in cmake cc c++ make pkg-config curl sha256sum tar flock readelf ldd file; do
    command -v "$command_name" >/dev/null || die "Werkzeug fehlt: $command_name (siehe --help)."
done
glibc="$(getconf GNU_LIBC_VERSION)"; glibc="${glibc#glibc }"
[[ $glibc =~ ^[0-9]+\.[0-9]+$ ]] || die 'Eine GNU-glibc-Buildumgebung wird benoetigt.'
newer_glibc=false
if [[ $(printf '%s\n' 2.35 "$glibc" | sort -V | tail -n 1) != 2.35 ]]; then
    "$allow_newer" || die "glibc $glibc ist neuer als die Release-Basis 2.35. Auf Mint 21 bauen; fuer einen lokalen Test --allow-newer-glibc verwenden."
    newer_glibc=true
fi
qmake="$(command -v "${QMAKE:-qmake}")" || die 'Qt-5-qmake fehlt (Paket qtbase5-dev).'
qt_version="$("$qmake" -query QT_VERSION)"
[[ $qt_version == 5.* ]] || die "Dieses Linux-Paket verwendet Qt 5, gefunden: $qt_version. QMAKE auf Qt-5-qmake setzen."
qt_libraries="$("$qmake" -query QT_INSTALL_LIBS)"
qt_plugins="$("$qmake" -query QT_INSTALL_PLUGINS)"
qt_translations="$("$qmake" -query QT_INSTALL_TRANSLATIONS)"
[[ -f $qt_plugins/imageformats/libqjpeg.so ]] || die 'Qt-JPEG-Plugin fehlt (siehe --help).'
for plugin in platforms/libqwayland-egl.so platforms/libqwayland-generic.so wayland-shell-integration/libxdg-shell.so; do
    [[ -f $qt_plugins/$plugin ]] || die "Qt-Wayland-Plugin fehlt: $plugin (Paket qtwayland5)."
done
[[ -f $qt_translations/qtbase_de.qm ]] || die 'Deutsche Qt-Uebersetzung fehlt (Paket qttranslations5-l10n).'
pkg-config --exists libpulse zlib || die 'libpulse-dev bzw. zlib1g-dev fehlt.'

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
version="$(sed -nE 's/^[[:space:]]*project\(hs_stv[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' "$project_dir/CMakeLists.txt")"
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die 'Projektversion konnte nicht gelesen werden.'
assets="$project_dir/packaging/appimage"
arch_dir="$project_dir/build-appimage/$arch"
cache_dir="$project_dir/build-appimage/cache"
tools_dir="$arch_dir/tools"
build_dir="$arch_dir/hs-stv"
liquid_build="$arch_dir/liquid-build"
liquid_prefix="$arch_dir/liquid-install"
app_dir="$arch_dir/AppDir"
output_dir="$arch_dir/output"
mkdir -p "$arch_dir" "$cache_dir" "$tools_dir" "$output_dir"
exec 9>"$arch_dir/.lock"
flock -n 9 || die "Fuer $arch laeuft bereits ein AppImage-Build."
exec > >(tee "$arch_dir/build.log") 2>&1
trap 'printf "AppImage-Build fehlgeschlagen (Zeile %s); siehe build-appimage/%s/build.log.\n" "$LINENO" "$arch" >&2' ERR
printf 'HS-STV %s | %s | glibc %s | Qt %s | %s Build-Jobs\n' "$version" "$arch" "$glibc" "$qt_version" "$jobs"
if "$newer_glibc"; then printf 'Entwicklungsbuild: Das Ergebnis ist nicht fuer die Mint-21-Basis freigegeben.\n'; fi

download() {
    local url="$1" destination="$2" digest="$3" temporary
    if [[ -f $destination ]] && printf '%s  %s\n' "$digest" "$destination" | sha256sum --check --status; then return; fi
    temporary="$(mktemp "$destination.download.XXXXXX")"
    printf 'Download: %s\n' "${url##*/}"
    if ! curl --fail --location --retry 3 --silent --show-error "$url" --output "$temporary" ||
       ! printf '%s  %s\n' "$digest" "$temporary" | sha256sum --check --status; then
        rm -f -- "$temporary"
        die "Download/Pruefsumme fehlgeschlagen: $url"
    fi
    chmod 644 "$temporary"
    mv -f -- "$temporary" "$destination"
}

liquid_digest=18fa83b73db8bb6fe6ea0376e4b5aecf8645970f4604d10d9dadbf609f3f95e2
liquid_source="$cache_dir/liquid-dsp-1.8.3"
# Only cache creation is shared. Neither compiler writes into these sources.
(
    flock 8
    download 'https://codeload.github.com/jgaeddert/liquid-dsp/tar.gz/refs/tags/v1.8.3' "$cache_dir/liquid-dsp-1.8.3.tar.gz" "$liquid_digest"
    if [[ ! -f $liquid_source/.source-sha256 ]] || [[ $(cat "$liquid_source/.source-sha256") != "$liquid_digest" ]]; then
        [[ ! -e $liquid_source ]] || die "Unbekannter liquid-dsp-Cache: $liquid_source"
        temporary_source="$(mktemp -d "$cache_dir/liquid-source.XXXXXX")"
        tar -xzf "$cache_dir/liquid-dsp-1.8.3.tar.gz" --strip-components=1 -C "$temporary_source"
        printf '%s\n' "$liquid_digest" > "$temporary_source/.source-sha256"
        mv -- "$temporary_source" "$liquid_source"
    fi
) 8>"$cache_dir/.lock"

while read -r tool_arch digest url; do
    [[ $tool_arch == "$arch" ]] || continue
    download "$url" "$tools_dir/${url##*/}" "$digest"
    chmod 755 "$tools_dir/${url##*/}"
done < "$assets/tools.lock"
ln -sfn "linuxdeploy-plugin-qt-$arch.AppImage" "$tools_dir/linuxdeploy-plugin-qt"

# Always discard this architecture's generated build state. Other architectures,
# source files, cached downloads and the last successful output are preserved.
rm -rf -- "$build_dir" "$liquid_build" "$liquid_prefix" "$app_dir"
if [[ $arch == x86_64 ]]; then
    cpu_flags=-march=x86-64
    simd_flags=(-DC_SSE_FOUND=ON -DC_SSE2_FOUND=ON)
else
    cpu_flags=-march=armv8-a
    simd_flags=(-DC_NEON_FOUND=ON)
fi
cmake -S "$liquid_source" -B "$liquid_build" \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$liquid_prefix" -DCMAKE_INSTALL_LIBDIR=lib \
    "-DCMAKE_C_FLAGS=$cpu_flags" "-DCMAKE_CXX_FLAGS=$cpu_flags" \
    -DBUILD_EXAMPLES=OFF -DBUILD_AUTOTESTS=OFF -DBUILD_BENCHMARKS=OFF \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DFIND_FFTW=OFF \
    -DENABLE_SIMD=ON -DFIND_SIMD=OFF "${simd_flags[@]}" \
    -DENABLE_LOGGING=OFF -DENABLE_TIMESTAMPS=OFF
cmake --build "$liquid_build" --parallel "$jobs"
cmake --install "$liquid_build"
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF -DMM_SSTV_BUILD_GUI=ON -DMM_SSTV_AUDIO_BACKEND=pulse \
    "-DCMAKE_CXX_FLAGS=$cpu_flags" "-DQT_DIR=$qt_libraries/cmake/Qt5" \
    "-DMM_SSTV_LIQUID_ROOT=$liquid_prefix" "-DMM_LIQUID_LIBRARY=$liquid_prefix/lib/libliquid.a"
cmake --build "$build_dir" --parallel "$jobs"
cmake --install "$build_dir" --prefix "$app_dir/usr"

# Deployment tools and their plugins also run without requiring FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$qmake"
export PATH="$tools_dir:$PATH"
# The Qt deployer's waylandcompositor module also deploys the client-side
# decoration, shell and graphics integration plugins required by QtWayland.
export EXTRA_QT_MODULES=waylandcompositor
export EXTRA_PLATFORM_PLUGINS='libqoffscreen.so;libqminimal.so;libqwayland-egl.so;libqwayland-generic.so'
"$tools_dir/linuxdeploy-$arch.AppImage" --appdir "$app_dir" \
    --deploy-deps-only "$app_dir/usr/bin" --plugin qt \
    --desktop-file "$assets/hs-stv.desktop" --icon-file "$assets/hs-stv.svg" \
    --custom-apprun "$assets/AppRun"

licenses="$app_dir/usr/share/licenses/HS-STV"
mkdir -p "$licenses" "$app_dir/usr/translations"
cp "$project_dir/LICENSE" "$licenses/LICENSE-HS-STV"
cp "$liquid_source/LICENSE" "$licenses/LICENSE-liquid-dsp"
cp "$project_dir/external/qo100-rx/LICENSE" "$licenses/LICENSE-qo100-rx"
cp "$project_dir/external/qo100-psk/COPYING" "$licenses/LICENSE-qo100-psk"
for license in LGPL-2.1 LGPL-3; do
    [[ ! -f /usr/share/common-licenses/$license ]] || cp "/usr/share/common-licenses/$license" "$licenses/$license"
done
# Retain distribution copyright notices for the bundled shared libraries.
if command -v dpkg-query >/dev/null; then
    for library in "$app_dir/usr/lib/"*.so*; do
        [[ -f $library ]] || continue
        packages="$(dpkg-query -S "*/${library##*/}" 2>/dev/null | sed 's/: \/.*//' | sort -u || true)"
        while IFS= read -r package; do
            package="${package%%:*}"
            [[ -f /usr/share/doc/$package/copyright ]] || continue
            cp "/usr/share/doc/$package/copyright" "$licenses/copyright-$package"
        done <<< "$packages"
    done
fi
cp "$qt_translations/qtbase_de.qm" "$app_dir/usr/translations/qtbase_de.qm"

required_programs=(hs-stv hs-stv-rx-core hs-stv-sstv-rx-core hs-stv-psk-rx-core)
for program in "${required_programs[@]}"; do
    [[ -x $app_dir/usr/bin/$program ]] || die "Programm fehlt: $program"
    machine="$(readelf -h "$app_dir/usr/bin/$program" | sed -n 's/.*Machine:[[:space:]]*//p')"
    if [[ $arch == x86_64 ]]; then [[ $machine == *X86-64* ]] || die "Falsche Architektur: $program ($machine)"
    else [[ $machine == AArch64 ]] || die "Falsche Architektur: $program ($machine)"; fi
    dependencies="$(ldd "$app_dir/usr/bin/$program")"
    [[ $dependencies != *'not found'* ]] || die "Fehlende Bibliothek fuer $program: $dependencies"
done
required_plugins=(imageformats/libqjpeg.so platforms/libqxcb.so
    platforms/libqwayland-egl.so platforms/libqwayland-generic.so
    wayland-shell-integration/libxdg-shell.so
    wayland-decoration-client/libbradient.so
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so)
for plugin in "${required_plugins[@]}"; do
    [[ -f $app_dir/usr/plugins/$plugin ]] || die "Qt-Plugin wurde nicht paketiert: $plugin"
    dependencies="$(ldd "$app_dir/usr/plugins/$plugin")"
    [[ $dependencies != *'not found'* ]] || die "Fehlende Bibliothek fuer Qt-Plugin $plugin: $dependencies"
done

suffix=; "$newer_glibc" && suffix=-development
filename="HS-STV-$version-$arch$suffix.AppImage"
build_info="$output_dir/${filename%.AppImage}.build-info.txt"
source_revision="$(git -C "$project_dir" rev-parse HEAD 2>/dev/null || printf 'unbekannt')"
{
    printf 'HS-STV: %s\nArchitektur: %s\nBuild-Host-glibc: %s\nQt: %s\nQuellstand: %s\n' "$version" "$arch" "$glibc" "$qt_version" "$source_revision"
    printf 'Build-Zeit (UTC): %s\nCPU-Basis: %s\n' "$(date -u '+%Y-%m-%d %H:%M:%S')" "$cpu_flags"
    printf 'Qt-Plattformen: X11/XWayland, Wayland\n'
    printf 'liquid-dsp: 1.8.3\nQuellen-SHA256: %s\n' "$liquid_digest"
    printf 'Gemeinsame HS-STV-Quellen: %s\nEntwicklungsbuild: %s\n' "$project_dir" "$newer_glibc"
    printf '\nPaketierungswerkzeuge:\n'
    cat "$assets/tools.lock"
} > "$build_info"
cp "$build_info" "$app_dir/usr/share/licenses/HS-STV/BUILD-INFO.txt"
export ARCH="$arch" VERSION="$version"
# Supply a fixed runtime instead of letting appimagetool download a new one.
"$tools_dir/appimagetool-$arch.AppImage" --no-appstream --comp zstd \
    --mksquashfs-opt=-processors "--mksquashfs-opt=$jobs" \
    --runtime-file "$tools_dir/runtime-$arch" "$app_dir" "$output_dir/$filename.new"
mv -f -- "$output_dir/$filename.new" "$output_dir/$filename"
chmod +x "$output_dir/$filename"
(cd "$output_dir" && sha256sum "$filename" > "$filename.sha256")
printf '\nFertig: %s\nPruefsumme: %s\nBuild-Informationen: %s\n' "$output_dir/$filename" "$output_dir/$filename.sha256" "$build_info"
