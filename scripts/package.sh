#!/usr/bin/env bash
# Package WayClack release artifacts into dist/.
#
# Produces:
#   dist/wayclack-<version>-linux-x86_64.tar.gz   (binary + README + LICENSE)
#   dist/wayclack-<version>-src.tar.gz            (git archive of the tag/HEAD)
#
# Usage: scripts/package.sh [version]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
  VERSION="$(
    grep -oP 'project\(wayclack VERSION \K[0-9.]+' CMakeLists.txt | head -1
  )"
fi
if [[ -z "$VERSION" ]]; then
  echo "Could not determine version; pass one explicitly." >&2
  exit 1
fi

ARCH="$(uname -m)"
PLATFORM="linux-${ARCH}"
DIST="$ROOT/dist"
STAGE="$DIST/wayclack-${VERSION}-${PLATFORM}"

echo "==> Building WayClack ${VERSION} (${PLATFORM})"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)"

echo "==> Staging binary bundle"
rm -rf "$STAGE"
mkdir -p "$STAGE"
install -m0755 build/wayclack "$STAGE/wayclack"
cp README.md LICENSE "$STAGE/"

echo "==> Creating dist/wayclack-${VERSION}-${PLATFORM}.tar.gz"
tar -C "$DIST" -czf "$DIST/wayclack-${VERSION}-${PLATFORM}.tar.gz" \
  "wayclack-${VERSION}-${PLATFORM}"

echo "==> Creating source archive dist/wayclack-${VERSION}-src.tar.gz"
# Prefer a clean git archive when inside a repo.
if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
  git archive --format=tar.gz \
    --prefix="wayclack-${VERSION}/" \
    -o "$DIST/wayclack-${VERSION}-src.tar.gz" HEAD
else
  tar --exclude='./build' --exclude='./dist' --exclude='./.git' \
    -C "$ROOT" -czf "$DIST/wayclack-${VERSION}-src.tar.gz" \
    --transform "s,^\.,wayclack-${VERSION}," .
fi

echo "==> Generating checksums"
( cd "$DIST" && sha256sum \
    "wayclack-${VERSION}-${PLATFORM}.tar.gz" \
    "wayclack-${VERSION}-src.tar.gz" > SHA256SUMS )

echo
echo "Artifacts in $DIST:"
ls -lh "$DIST"
echo
echo "Checksums:"
cat "$DIST/SHA256SUMS"
