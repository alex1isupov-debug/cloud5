#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
    echo "Run this script from an MSYS2 MINGW64 environment." >&2
    exit 1
fi

export PATH="/mingw64/bin:/usr/bin:${PATH}"

cmake \
    -S "$repo_root" \
    -B "$repo_root/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCHIAKI_ENABLE_CLI=OFF

cmake --build "$repo_root/build" --config Release --target chiaki

echo "Built: $repo_root/build/gui/cloudgta-player.exe"
