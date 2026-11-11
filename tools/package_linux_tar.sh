#!/bin/sh
# Package the AppImage with a ready-to-use Linux directory structure.
#
#   sh tools/package_linux_tar.sh 1.4
#
set -eu

VERSION=${1:-1.4}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

APPIMAGE="$ROOT/dist/LikeTheWind-${VERSION}-x86_64.AppImage"
if [ ! -f "$APPIMAGE" ]; then
    echo "missing $APPIMAGE; build the AppImage first" >&2
    exit 1
fi

NAME="LikeTheWind-${VERSION}-linux-x86_64"
STAGE_ROOT="$ROOT/work/tmp/linux-tar"
STAGE="$STAGE_ROOT/$NAME"
OUT="$ROOT/dist/${NAME}.tar.gz"

rm -rf "$STAGE"
mkdir -p "$STAGE/roms" "$STAGE/licenses"
cp "$APPIMAGE" "$STAGE/LikeTheWind.AppImage"
chmod +x "$STAGE/LikeTheWind.AppImage"
cp "$ROOT/packaging/linux/README.txt" "$STAGE/README.txt"
cp "$ROOT/packaging/linux/roms/README.txt" "$STAGE/roms/README.txt"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/"
cp "$ROOT/licenses/lgpl-2.1.txt" "$STAGE/licenses/"

tar -C "$STAGE_ROOT" -czf "$OUT" "$NAME"
echo "Linux bundle: $OUT"
