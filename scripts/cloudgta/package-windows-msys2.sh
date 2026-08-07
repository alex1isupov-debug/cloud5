#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_dir="$repo_root/dist/cloudgta-player-lab-win64"
archive="$repo_root/dist/cloudgta-player-lab-win64.zip"

if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
    echo "Run this script from an MSYS2 MINGW64 environment." >&2
    exit 1
fi

if [[ ! -f "$repo_root/build/gui/cloudgta-player.exe" ]]; then
    echo "Build cloudgta-player.exe before packaging." >&2
    exit 1
fi

mkdir -p "$output_dir"

"$repo_root/scripts/deploy-windows-msys2.sh" \
    "$output_dir" \
    "$repo_root/build/gui/cloudgta-player.exe" \
    "$repo_root/build/third-party/cpp-steam-tools" \
    /mingw64 \
    "$repo_root/gui/src/qml"

cp "$repo_root/scripts/cloudgta/CloudGTA-Player-Lab.cmd" "$output_dir/"
cp "$repo_root/scripts/cloudgta/LAB-README-RU.txt" "$output_dir/"
cp "$repo_root/COPYING" "$output_dir/LICENSE.txt"

rm -f "$archive"
(
    cd "$repo_root/dist"
    zip -qr "$(basename "$archive")" "$(basename "$output_dir")"
)

echo "Packaged: $archive"
