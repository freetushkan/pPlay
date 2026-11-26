#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CORE="${P2PLAY_EMBEDDED_SOURCE_DIR:-$PWD/third_party/P2Play}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# The core must have been prepared with ci/prepare-p2play-embedded.sh.
git -C "$CORE" apply --reverse --check "$PWD/ci/patches/p2play-http-shutdown.patch"
g++ -std=c++17 -pthread -I"$CORE/src/core" tests/p2play_http_shutdown.cpp "$CORE/src/core/http.cpp" -o "$TMP/http"
for scenario in 0 1 2; do timeout 5 "$TMP/http" "$scenario"; done
incs=(-Ilibcross2d/include -Ilibcross2d/source/platforms/glad/gl2/include)
while IFS= read -r dir; do incs+=("-I$dir"); done < <(find src -type d)
read -ra deps <<< "$(pkg-config --cflags sdl2 freetype2 libpng mpv libavformat libavcodec libavfilter libavutil libass libcurl)"
flags=(-std=c++17 -O1 -D__LINUX__ -D__SDL2__ -D__GL2__ -D__GLAD__)
g++ "${flags[@]}" "${incs[@]}" "${deps[@]}" -pthread -I"$CORE/src/core" \
    tests/p2play_embedded_shutdown.cpp src/net/p2play_embedded.cpp "$CORE/src/core/http.cpp" -o "$TMP/embedded"
timeout 5 "$TMP/embedded"
timeout 5 "$TMP/embedded" delayed
g++ "${flags[@]}" "${incs[@]}" "${deps[@]}" -ffunction-sections -fdata-sections \
    tests/mpv_resync.cpp src/player/mpv_props.cpp -Wl,--gc-sections,--wrap=mpv_get_property -lmpv -pthread -o "$TMP/resync"
ffmpeg -hide_banner -loglevel error -f lavfi -i 'color=c=blue:s=160x90:r=24:d=8' -c:v mpeg4 -y "$TMP/test.mp4"
timeout 15 "$TMP/resync" "$TMP/test.mp4"
python3 tests/mpv_keep_open_smoke.py
