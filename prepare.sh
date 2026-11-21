#!/usr/bin/env bash
# Debian-/Ubuntu-/Mint-Pakete fuer Bau, Betrieb und die normalen Tests von HS-STV.
set -euo pipefail

usage() {
    cat <<'EOF'
Aufruf: ./prepare.sh [--dry-run] [--portaudio]

  --dry-run    Paketinstallation nur simulieren, ohne sudo oder apt update.
  --portaudio  Auch die Entwicklungsdateien fuer den optionalen PortAudio-Build.
  -h, --help   Diese Hilfe anzeigen.

Ohne Optionen werden Paketlisten aktualisiert und alle benoetigten Pakete
installiert. Root-Rechte werden bei Bedarf mit sudo angefordert.
EOF
}

dry_run=false
portaudio=false
for arg in "$@"; do
    case "$arg" in
        --dry-run) dry_run=true ;;
        --portaudio) portaudio=true ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unbekannte Option: %s\n' "$arg" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -f /etc/debian_version ]] || ! command -v apt-get >/dev/null 2>&1; then
    printf 'Dieses Skript benoetigt Debian, Ubuntu oder Linux Mint mit apt.\n' >&2
    exit 1
fi

packages=(
    # C/C++-Laufzeit, insbesondere glibc (Debian-Paketname: libc6).
    libc6 libc6-dev libgcc-s1 libstdc++6
    # start.sh baut das Programm beim ersten Start automatisch.
    build-essential cmake pkg-config
    # Qt Widgets, Qt Test, Bildformate und Wayland-Unterstuetzung.
    qtbase5-dev qt5-image-formats-plugins qtwayland5
    fonts-dejavu-core xdg-utils
    # PulseAudio-Client: funktioniert auch mit PipeWire-Pulse.
    libpulse0 libpulse-dev pulseaudio-utils
    # build.sh aktiviert die Tests; diese verwenden NumPy.
    python3 python3-numpy
)
if "$portaudio"; then
    packages+=(portaudio19-dev)
fi

if "$dry_run"; then
    exec apt-get --simulate --no-remove --no-install-recommends install "${packages[@]}"
fi

as_root=()
if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Bitte als root ausfuehren; sudo ist nicht installiert.\n' >&2
        exit 1
    fi
    as_root=(sudo)
fi

"${as_root[@]}" apt-get update
"${as_root[@]}" apt-get install --yes --no-remove --no-install-recommends "${packages[@]}"

cat <<'EOF'

Pakete installiert. Im Projektverzeichnis als normaler Benutzer fortfahren:
  ./build.sh -DPython3_EXECUTABLE=/usr/bin/python3
  ./start.sh

Ein laufender PulseAudio- oder PipeWire-Pulse-Server wird vorausgesetzt.
Die virtuellen Geraete toQO100/fromQO100 liefert der QO100_TRX2-Audioadapter.
EOF
if "$portaudio"; then
    printf '\nOptionaler PortAudio-Build:\n  ./build.sh -DMM_SSTV_AUDIO_BACKEND=portaudio -DPython3_EXECUTABLE=/usr/bin/python3\n'
fi
