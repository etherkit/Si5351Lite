#!/bin/sh
# Build a source tar and git bundle from HEAD and check them against the commit.
# Usage: tools/identity.sh /path/to/output-dir
set -e
cd "$(dirname "$0")/.."
OUT=${1:-/opt/cursor/artifacts}
mkdir -p "$OUT"
HEAD=$(git rev-parse HEAD)
BRANCH=$(git rev-parse --abbrev-ref HEAD)
TAR="$OUT/Si5351Lite-src.tar.gz"
BUNDLE="$OUT/Si5351Lite-${BRANCH}.bundle"
MAN="$OUT/MANIFEST-${HEAD}.txt"

git archive --format=tar.gz --prefix=Si5351Lite/ -o "$TAR" HEAD
git bundle create "$BUNDLE" "$BRANCH"
git bundle verify "$BUNDLE" >/dev/null

WORK=$(mktemp -d)
tar -xzf "$TAR" -C "$WORK"
FAIL=0
git ls-tree -r HEAD | while read -r mode type hash path; do
  file="$WORK/Si5351Lite/$path"
  if [ ! -f "$file" ]; then
    echo "missing in tar: $path" >&2
    echo FAIL >>"$WORK/flag"
    continue
  fi
  got=$(git hash-object "$file")
  if [ "$got" != "$hash" ]; then
    echo "hash mismatch $path commit=$hash archive=$got" >&2
    echo FAIL >>"$WORK/flag"
  fi
done
if [ -f "$WORK/flag" ]; then
  FAIL=1
fi

# Archive must not contain paths the commit does not.
tar -tzf "$TAR" | sed 's#^Si5351Lite/##' | sed '/^$/d' | while read -r path; do
  case "$path" in
    */) continue ;;
  esac
  if ! git cat-file -e "HEAD:$path" 2>/dev/null; then
    echo "tar has extra path: $path" >&2
    echo FAIL >>"$WORK/flag"
  fi
done
if [ -f "$WORK/flag" ]; then
  FAIL=1
fi

{
  echo "branch $BRANCH"
  echo "head $HEAD"
  echo "tar $(sha256sum "$TAR" | awk '{print $1}')"
  echo "bundle $(sha256sum "$BUNDLE" | awk '{print $1}')"
  echo "files $(git ls-tree -r --name-only HEAD | wc -l)"
  git ls-tree -r HEAD
} >"$MAN"

rm -rf "$WORK"
if [ "$FAIL" != 0 ]; then
  echo "IDENTITY FAIL" >&2
  exit 1
fi
echo "IDENTITY PASS $HEAD"
echo "manifest $MAN"
