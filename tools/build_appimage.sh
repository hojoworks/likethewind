#!/bin/sh
# Build the Linux AppImage. Run from the repository root in a Linux environment
# (WSL is fine):
#
#   sh tools/build_appimage.sh 1.2
#
set -eu

VERSION=${1:-1.2}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

BIN="$ROOT/build/LikeTheWind-linux"
SDL="$ROOT/build/libSDL2-2.0.so.0"
if [ ! -f "$BIN" ] || [ ! -f "$SDL" ]; then
    echo "build/LikeTheWind-linux is missing; run tools/build_linux.ps1 first" >&2
    exit 1
fi

TOOLS="$ROOT/vendor/appimage"
APPDIR="$ROOT/work/tmp/AppDir"
LINUXDEPLOY="$TOOLS/linuxdeploy"
APPIMAGETOOL="$TOOLS/appimagetool"
mkdir -p "$TOOLS"
if [ ! -x "$LINUXDEPLOY" ]; then
    curl -fL -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi
if [ ! -x "$APPIMAGETOOL" ]; then
    curl -fL -o "$APPIMAGETOOL" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$APPIMAGETOOL"
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/likethewind"
cp "$BIN" "$APPDIR/usr/bin/LikeTheWind-linux"
cp "$SDL" "$APPDIR/usr/lib/libSDL2-2.0.so.0"
cp -R samples mixes "$APPDIR/usr/share/likethewind/"
if [ -f gamecontrollerdb.txt ]; then
    cp gamecontrollerdb.txt "$APPDIR/usr/share/likethewind/"
fi
mkdir -p "$APPDIR/usr/share/doc/likethewind/licenses"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APPDIR/usr/share/doc/likethewind/"
cp "$ROOT/licenses/lgpl-2.1.txt" "$APPDIR/usr/share/doc/likethewind/licenses/"
chmod +x "$APPDIR/usr/bin/LikeTheWind-linux"

# Keep ALSA's configuration and plugins with the bundled audio stack. This is
# what lets the AppImage start on a desktop that only has PipeWire/Pulse and no
# system libasound2 package.
if [ -d /usr/share/alsa ]; then
    cp -R /usr/share/alsa "$APPDIR/usr/share/alsa"
fi
if [ -d /usr/lib/x86_64-linux-gnu/alsa-lib ]; then
    mkdir -p "$APPDIR/usr/lib/alsa-lib"
    cp -R /usr/lib/x86_64-linux-gnu/alsa-lib/* "$APPDIR/usr/lib/alsa-lib/"
fi

APPIMAGE_EXTRACT_AND_RUN=1 LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/LikeTheWind-linux" \
    --library "$APPDIR/usr/lib/libSDL2-2.0.so.0" \
    --desktop-file "$ROOT/packaging/likethewind.desktop" \
    --icon-file "$ROOT/packaging/likethewind.png" \
    --custom-apprun "$ROOT/packaging/AppRun"

chmod +x "$APPDIR/AppRun" "$APPDIR/usr/bin/LikeTheWind-linux"
# linuxdeploy treats libasound as a base-system library and leaves it out.
# Keep the exact runtime beside the bundled SDL2 so the AppImage starts even
# when the host has no ALSA package installed.
cp -L /usr/lib/x86_64-linux-gnu/libasound.so.2 "$APPDIR/usr/lib/libasound.so.2"

# linuxdeploy intentionally leaves base desktop libraries such as libX11 to
# the host. For a truly self-contained Linux release, pull in the full
# non-glibc dependency closure instead of chasing the next missing SONAME.
copy_closure() {
    ldd "$APPDIR/usr/bin/LikeTheWind-linux" "$APPDIR"/usr/lib/*.so* 2>/dev/null |
        awk '/=> \// { print $3 }' | sort -u |
        while read -r lib; do
            base=$(basename "$lib")
            case "$base" in
                libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux*)
                    continue
                    ;;
            esac
            if [ ! -e "$APPDIR/usr/lib/$base" ]; then
                cp -L "$lib" "$APPDIR/usr/lib/$base"
            fi
        done
}
copy_closure
copy_closure
copy_closure

mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/LikeTheWind-${VERSION}-x86_64.AppImage"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --no-appstream "$APPDIR" "$OUT"
echo "AppImage: $OUT"
