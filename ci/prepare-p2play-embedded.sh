#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE_SHA="${P2PLAY_CORE_SHA:?P2PLAY_CORE_SHA must be selected by the GitHub Actions workflow}"
REPOSITORY="${P2PLAY_CORE_REPOSITORY:?P2PLAY_CORE_REPOSITORY must be set by the workflow}"
BOOST_VERSION="${P2PLAY_BOOST_VERSION:?P2PLAY_BOOST_VERSION must be set by the workflow}"
BOOST="${P2PLAY_BOOST_INCLUDE_DIR:-.cache/p2play-boost-headers}"

if [[ ! "$CORE_SHA" =~ ^[0-9a-f]{40}$ ]]; then
    echo "P2PLAY_CORE_SHA must be a full 40-character commit SHA: $CORE_SHA" >&2
    exit 1
fi
case "$BOOST" in
    /*) ;;
    *) BOOST="$ROOT/$BOOST" ;;
esac
CORE="$ROOT/third_party/P2Play"

mkdir -p "$ROOT/third_party"
if [ ! -d "$CORE/.git" ]; then
    git clone --no-checkout "$REPOSITORY" "$CORE"
fi
git -C "$CORE" remote set-url origin "$REPOSITORY"
# Fetch master so an already-resolved commit remains available even if master moves
# while platform jobs are running. Fall back to an exact SHA fetch for a detached lock.
git -C "$CORE" fetch --no-tags origin +refs/heads/master:refs/remotes/origin/master
if ! git -C "$CORE" cat-file -e "$CORE_SHA^{commit}" 2>/dev/null; then
    git -C "$CORE" fetch --no-tags origin "$CORE_SHA"
fi
git -C "$CORE" checkout --force --detach "$CORE_SHA"
CHECKED_OUT_SHA="$(git -C "$CORE" rev-parse HEAD)"
if [ "$CHECKED_OUT_SHA" != "$CORE_SHA" ]; then
    echo "P2Play checkout mismatch: expected $CORE_SHA, got $CHECKED_OUT_SHA" >&2
    exit 1
fi
echo "Embedded P2Play core: $CHECKED_OUT_SHA ($REPOSITORY)"
git -C "$CORE" submodule sync --recursive
git -C "$CORE" submodule update --init --recursive
# pPlay-owned compatibility fix for the workflow-selected embedded core. Idempotent on CI caches;
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
"$CORE/ci/boost-headers.sh" "$BOOST_VERSION" "$BOOST"

export P2PLAY_EMBEDDED_SOURCE_DIR="$CORE"
export P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR="$BOOST"
if [ -n "${GITHUB_ENV:-}" ]; then
    printf 'P2PLAY_EMBEDDED_SOURCE_DIR=%s\n' "$CORE" >> "$GITHUB_ENV"
    printf 'P2PLAY_EMBEDDED_BOOST_INCLUDE_DIR=%s\n' "$BOOST" >> "$GITHUB_ENV"
fi
