#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$project_dir/build/bin/mm-sstv" ]]; then
    "$project_dir/build.sh"
fi
exec "$project_dir/build/bin/mm-sstv" "$@"
