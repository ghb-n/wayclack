#!/usr/bin/env bash
# Cut a WayClack release: verify, tag, and (optionally) push + publish.
#
# Steps:
#   1. Verify the tree builds warning-free.
#   2. Create an annotated tag v<version>.
#   3. Package artifacts into dist/.
#   4. Print (or run) the commands to push and publish a GitHub release.
#
# Usage:
#   scripts/release.sh                     # tag + package, print next steps
#   scripts/release.sh --push              # also push branch + tag
#   scripts/release.sh --publish           # also create the GitHub release (needs gh)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PUSH=0
PUBLISH=0
for arg in "$@"; do
  case "$arg" in
    --push) PUSH=1 ;;
    --publish) PUBLISH=1; PUSH=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

VERSION="$(grep -oP 'project\(wayclack VERSION \K[0-9.]+' CMakeLists.txt | head -1)"
TAG="v${VERSION}"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
NOTES="RELEASE_NOTES.md"

if [[ -z "$VERSION" ]]; then
  echo "Could not determine version from CMakeLists.txt" >&2
  exit 1
fi

echo "==> Release ${TAG} from branch '${BRANCH}'"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "!! Working tree has uncommitted changes. Commit them first." >&2
  git status --short >&2
  exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "!! Tag ${TAG} already exists." >&2
  exit 1
fi

echo "==> Verifying a clean, warning-free build"
BUILD_LOG="$(mktemp)"
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null; then
  echo "!! Configure failed" >&2; exit 1
fi
if ! cmake --build build -j"$(nproc)" 2>&1 | tee "$BUILD_LOG" >/dev/null; then
  echo "!! Build failed" >&2; cat "$BUILD_LOG" >&2; exit 1
fi
if grep -qE 'warning:|error:' "$BUILD_LOG"; then
  echo "!! Build produced warnings:" >&2
  grep -E 'warning:|error:' "$BUILD_LOG" >&2
  exit 1
fi
rm -f "$BUILD_LOG"
echo "    build is clean"

echo "==> Creating annotated tag ${TAG}"
git tag -a "$TAG" -m "WayClack ${TAG}"

echo "==> Packaging artifacts"
scripts/package.sh "$VERSION"

REMOTE="$(git remote | head -1 || true)"
REMOTE="${REMOTE:-origin}"

echo
echo "=============================================================="
echo " Release ${TAG} prepared locally."
echo "=============================================================="
echo
echo "1) Push the branch and tag:"
echo "     git push ${REMOTE} ${BRANCH}"
echo "     git push ${REMOTE} ${TAG}"
echo
echo "2) Publish the GitHub release (requires the 'gh' CLI):"
echo "     gh release create ${TAG} \\"
echo "       dist/wayclack-*-linux-*.tar.gz \\"
echo "       dist/wayclack-*-src.tar.gz \\"
echo "       dist/SHA256SUMS \\"
echo "       --title \"WayClack ${TAG}\" \\"
echo "       --notes-file ${NOTES}"
echo
echo "   ...or, without 'gh', use the GitHub web UI:"
echo "     https://github.com/<owner>/<repo>/releases/new?tag=${TAG}"
echo

if [[ "$PUSH" -eq 1 ]]; then
  echo "==> Pushing ${BRANCH} and ${TAG} to ${REMOTE}"
  git push "$REMOTE" "$BRANCH"
  git push "$REMOTE" "$TAG"
fi

if [[ "$PUBLISH" -eq 1 ]]; then
  if ! command -v gh >/dev/null 2>&1; then
    echo "!! 'gh' CLI not found; skipping release publish." >&2
    exit 1
  fi
  echo "==> Creating GitHub release ${TAG}"
  gh release create "$TAG" \
    dist/wayclack-*-linux-*.tar.gz \
    dist/wayclack-*-src.tar.gz \
    dist/SHA256SUMS \
    --title "WayClack ${TAG}" \
    --notes-file "$NOTES"
fi
