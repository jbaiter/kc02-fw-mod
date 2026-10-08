#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
PREFIX="$ROOT/prefix"
export PATH="$ROOT/host-deps/usr/bin:$PREFIX/bin:$PATH"
export BISON_PKGDATADIR="$ROOT/host-deps/usr/share/bison"
export CC=${HOST_CC:-gcc-15}
export CXX=${HOST_CXX:-g++-15}
export CFLAGS='-O2 -std=gnu89 -fcommon'
export CXXFLAGS='-O2 -std=gnu++98 -fpermissive'
export MAKEINFO=true
JOBS=${JOBS:-8}
mkdir -p "$ROOT/build/binutils" "$ROOT/build/gcc" "$PREFIX" "$ROOT/logs"
case "${1:-}" in
  binutils)
    cd "$ROOT/build/binutils"
    if [[ ! -f Makefile ]]; then
      "$ROOT/src/binutils/configure" --target=or1k-elf --prefix="$PREFIX" \
        --disable-nls --disable-werror --disable-gdb --disable-sim \
        --disable-gprof --disable-shared 2>&1 | tee "$ROOT/logs/binutils-configure.log"
    fi
    make -j"$JOBS" MAKEINFO=true all-binutils all-gas all-ld 2>&1 | tee "$ROOT/logs/binutils-build.log"
    make MAKEINFO=true install-binutils install-gas install-ld 2>&1 | tee "$ROOT/logs/binutils-install.log"
    ;;
  gcc)
    cd "$ROOT/build/gcc"
    if [[ ! -f Makefile ]]; then
      "$ROOT/src/gcc/configure" --target=or1k-elf --prefix="$PREFIX" \
        --enable-languages=c --with-newlib --without-headers \
        --disable-bootstrap --disable-nls --disable-shared --disable-threads \
        --disable-libssp --disable-libquadmath --disable-libatomic \
        --disable-libgomp --disable-libffi --disable-libsanitizer \
        --with-mpfr="$ROOT/host-deps/usr" --with-mpc="$ROOT/host-deps/usr" \
        2>&1 | tee "$ROOT/logs/gcc-configure.log"
    fi
    make -j"$JOBS" MAKEINFO=true all-gcc 2>&1 | tee "$ROOT/logs/gcc-build.log"
    make MAKEINFO=true install-gcc 2>&1 | tee "$ROOT/logs/gcc-install.log"
    ;;
  newlib)
    mkdir -p "$ROOT/build/newlib"
    cd "$ROOT/build/newlib"
    if [[ ! -f Makefile ]]; then
      "$ROOT/src/newlib/configure" --target=or1k-elf --prefix="$PREFIX" \
        --disable-nls --disable-libgloss --disable-newlib-supplied-syscalls \
        2>&1 | tee "$ROOT/logs/newlib-configure.log"
    fi
    make -j"$JOBS" MAKEINFO=true CFLAGS_FOR_TARGET='-O2 -mhard-div' all-target-newlib \
      2>&1 | tee "$ROOT/logs/newlib-build.log"
    make MAKEINFO=true install-target-newlib 2>&1 | tee "$ROOT/logs/newlib-install.log"
    # SDK startup files need this macro header, normally installed by libgloss.
    install -m644 "$ROOT/src/newlib/libgloss/or1k/include/or1k-asm.h" \
      "$PREFIX/or1k-elf/include/or1k-asm.h"
    ;;
  libgcc)
    cd "$ROOT/build/gcc"
    make -j"$JOBS" MAKEINFO=true all-target-libgcc 2>&1 | tee "$ROOT/logs/libgcc-build.log"
    make MAKEINFO=true install-target-libgcc 2>&1 | tee "$ROOT/logs/libgcc-install.log"
    ;;
  *) echo "Usage: $0 {binutils|gcc|newlib|libgcc}" >&2; exit 2 ;;
esac
