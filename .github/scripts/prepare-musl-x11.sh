#!/usr/bin/env bash
# Stage the X11 archives missing from Dyne musl v2.7.0.
set -euo pipefail

work=$(realpath -m "${1:?pass a scratch directory outside the source tree}")
: "${CC:?set CC to the Dyne x86_64-linux-musl compiler}"
repo=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
if [[ "$work" == "$repo" || "$work" == "$repo/"* ]]; then
  echo 'Use a scratch directory outside the source tree.' >&2
  exit 1
fi
if [[ "$("$CC" -dumpmachine)" != x86_64-linux-musl ]]; then
  echo 'CC must target x86_64-linux-musl.' >&2
  exit 1
fi
mkdir -p "$work"
cd "$work"

fetch() {
  local url=$1 checksum=$2 file=${3:-${1##*/}}
  curl --fail --silent --show-error --location --retry 3 --connect-timeout 15 --max-time 120 "$url" -o "$file"
  printf '%s  %s\n' "$checksum" "$file" | sha256sum --check
}

mirror=https://dl-cdn.alpinelinux.org/alpine/v3.23
bootstrap=alpine-minirootfs-3.23.8-x86_64.tar.gz
fetch "$mirror/releases/x86_64/$bootstrap" \
  56e2810205c17a5dff78cdd7b2c2af4ccdef16f18d765d5bb0f527438b136676
mkdir -p bootstrap sysroot
tar --extract --gzip --file "$bootstrap" --directory bootstrap --no-same-owner
printf '%s/main\n' "$mirror" > repositories

# Use the verified bootstrap's loader and Alpine's signed package repositories.
# Packages are staged without privileges or installation scripts.
"$work/bootstrap/lib/ld-musl-x86_64.so.1" \
  --library-path "$work/bootstrap/lib:$work/bootstrap/usr/lib" "$work/bootstrap/sbin/apk" \
  --root "$work/sysroot" --initdb --keys-dir "$work/bootstrap/etc/apk/keys" \
  --repositories-file "$work/repositories" --no-scripts --no-chown --no-cache add \
  libx11-dev libx11-static libxext-dev libxext-static libxcb-static

# Alpine's libxau-dev ships headers and pkg-config metadata but no archive.
fetch https://www.x.org/releases/individual/lib/libXau-1.0.12.tar.xz \
  74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb
tar --extract --xz --file libXau-1.0.12.tar.xz
cd libXau-1.0.12
# Relocate metadata per package so CMake can search this overlay and the
# toolchain's dependency prefix together.
for pc in "$work/sysroot/usr/"{lib,share}/pkgconfig/*.pc; do
  [[ -f "$pc" ]] || continue
  sed -i 's@^prefix=/usr$@prefix=${pcfiledir}/../..@' "$pc"
done
export PKG_CONFIG_LIBDIR="$work/sysroot/usr/lib/pkgconfig:$work/sysroot/usr/share/pkgconfig"
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
LDFLAGS=-static ./configure --host=x86_64-linux-musl --prefix=/usr --disable-shared --enable-static
make -j2
install -m 0644 .libs/libXau.a "$work/sysroot/usr/lib/"
