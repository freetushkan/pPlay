#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="${P2PLAY_CORE_REF:-b6c7b74916e36a873663fa55d27a9aaa209e7cfe}"
BRANCH="${P2PLAY_CORE_BRANCH:-master}"
BOOST="${P2PLAY_BOOST_INCLUDE_DIR:-.cache/p2play-boost-headers}"
case "$BOOST" in
    /*) ;;
    *) BOOST="$ROOT/$BOOST" ;;
esac
CORE="$ROOT/third_party/P2Play"

mkdir -p "$ROOT/third_party"
if [ ! -d "$CORE/.git" ]; then
    git clone --no-checkout https://github.com/freetushkan/P2Play.git "$CORE"
fi
git -C "$CORE" fetch --no-tags origin "$BRANCH"
git -C "$CORE" checkout --detach "$REF"
git -C "$CORE" submodule sync --recursive
git -C "$CORE" submodule update --init --recursive
"$CORE/ci/boost-headers.sh" 1.86.0 "$BOOST"

export P2PLAY_EMBEDDED_SOURCE_DIR="$CORE"
export P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR="$BOOST"
if [ -n "${GITHUB_ENV:-}" ]; then
    printf 'P2PLAY_EMBEDDED_SOURCE_DIR=%s\n' "$CORE" >> "$GITHUB_ENV"
    printf 'P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR=%s\n' "$BOOST" >> "$GITHUB_ENV"
fi
