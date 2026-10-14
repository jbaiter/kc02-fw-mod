#!/bin/sh
# Fetch, patch and build the QEMU used by tools/emu.
#
# The upstream release tarball is used rather than a distribution source
# package: Debian's "+ds" repack strips pc-bios, which the build requires.
#
# Result: $QEMU_SRC/build/qemu-system-or1k, with an "ax3292" machine.
#
#   tools/emu/qemu/build-qemu.sh          # ~2 minutes on 16 cores
#   QEMU_VERSION=11.1.2 QEMU_SRC=~/qemu tools/emu/qemu/build-qemu.sh
set -eu

VERSION=${QEMU_VERSION:-11.1.2}
QEMU_SRC=${QEMU_SRC:-"$HOME/.src/qemu-$VERSION"}
TARBALL=${QEMU_TARBALL:-/tmp/qemu-$VERSION.tar.xz}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case ${QEMU_DISPLAY:-auto} in
    auto) SDL_CONFIG=; SDL_FEATURE=auto ;;
    sdl) SDL_CONFIG=--enable-sdl; SDL_FEATURE=enabled ;;
    none) SDL_CONFIG=--disable-sdl; SDL_FEATURE=disabled ;;
    *) echo 'QEMU_DISPLAY must be auto, sdl or none' >&2; exit 2 ;;
esac
PATCH_SHA=$(sha256sum "$HERE/0001-ax3292-machine.patch" | awk '{print $1}')

if [ ! -d "$QEMU_SRC" ]; then
    if [ ! -f "$TARBALL" ]; then
        echo "fetching qemu-$VERSION.tar.xz"
        curl -fSL -o "$TARBALL" "https://download.qemu.org/qemu-$VERSION.tar.xz"
    fi
    mkdir -p "$QEMU_SRC"
    tar -xJf "$TARBALL" --strip-components=1 -C "$QEMU_SRC"
fi

cd "$QEMU_SRC"

if [ -f .ax3292-patched ]; then
    STORED_SHA=
    read -r STORED_SHA < .ax3292-patched || true
    if [ "$STORED_SHA" != "$PATCH_SHA" ]; then
        echo "Source contains an older AX3292 patch; use a fresh QEMU_SRC directory." >&2
        exit 2
    fi
    echo "current patch already applied"
else
    patch -p1 --forward < "$HERE/0001-ax3292-machine.patch"
    printf '%s\n' "$PATCH_SHA" > .ax3292-patched
fi

mkdir -p build
cd build

if [ ! -f build.ninja ]; then
    ../configure \
        --target-list=or1k-softmmu \
        --disable-docs --disable-guest-agent --disable-tools \
        --disable-gtk $SDL_CONFIG --disable-sdl-image --disable-vnc --disable-spice \
        --disable-opengl --disable-curl --disable-libssh \
        --disable-brlapi --disable-capstone --disable-slirp
fi

./pyvenv/bin/meson configure -Dsdl="$SDL_FEATURE" -Dsdl_image=disabled
ninja -j"$JOBS" qemu-system-or1k
echo "built: $QEMU_SRC/build/qemu-system-or1k"
echo "run:   tools/emu/run.py firmware/original.bin --qemu $QEMU_SRC/build/qemu-system-or1k"
