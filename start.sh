#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$project_dir/build/bin/hs-stv" ]]; then
    "$project_dir/build.sh"
fi
cd -- "$project_dir"
exec "$project_dir/build/bin/hs-stv" "$@"
