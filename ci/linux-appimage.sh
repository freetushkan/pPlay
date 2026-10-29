#!/usr/bin/env bash

set -euo pipefail

BUILD="$(realpath "$1")"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
APPDIR="$BUILD/AppDir"
TOOL="$BUILD/appimagetool"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
cp -r "$BUILD/release/pplay/." "$APPDIR/usr/bin/"
ln -s usr/bin/pplay "$APPDIR/AppRun"
cp "$SRC/data/ps4/romfs/sce_sys/icon0.png" "$APPDIR/pplay.png"
cat > "$APPDIR/pplay.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=pPlay
Comment=Media player
Exec=pplay
Icon=pplay
Categories=AudioVideo;Video;Player;
Terminal=false
DESKTOP

if [ ! -x "$TOOL" ]; then
    curl -fsSL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
    chmod +x "$TOOL"
fi
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$BUILD/pPlay-x86_64.AppImage"
