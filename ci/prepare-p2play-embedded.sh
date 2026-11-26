#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="${P2PLAY_CORE_REF:-e33886c383cd0e9d9645777ec1ac73a18711a88f}"
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
# pPlay-owned compatibility fix for the pinned embedded core. Idempotent on CI caches;
# refuse an incompatible checkout instead of silently building without the fix.
PATCH="$ROOT/ci/patches/p2play-http-shutdown.patch"
if git -C "$CORE" apply --reverse --check "$PATCH" 2>/dev/null; then
    : # already applied
elif git -C "$CORE" apply --check "$PATCH"; then
    git -C "$CORE" apply "$PATCH"
else
    echo "Embedded HTTP shutdown patch does not match P2Play checkout" >&2
    exit 1
fi
"$CORE/ci/boost-headers.sh" 1.86.0 "$BOOST"

export P2PLAY_EMBEDDED_SOURCE_DIR="$CORE"
export P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR="$BOOST"
if [ -n "${GITHUB_ENV:-}" ]; then
    printf 'P2PLAY_EMBEDDED_SOURCE_DIR=%s\n' "$CORE" >> "$GITHUB_ENV"
    printf 'P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR=%s\n' "$BOOST" >> "$GITHUB_ENV"
fi
