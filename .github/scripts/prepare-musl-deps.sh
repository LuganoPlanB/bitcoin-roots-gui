#!/usr/bin/env bash
# Stage build-only x86-64 Alpine archives outside the source tree.
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
  glib-dev glib-static libx11-dev libx11-static libxext-dev libxext-static libxcb-static \
  openssl-dev openssl-libs-static libsodium-dev libsodium-static pcre2-static \
  zlib-static libffi-dev gettext-static bzip2-static util-linux-static

# Alpine's libxau-dev ships headers and pkg-config metadata but no archive.
fetch https://www.x.org/releases/individual/lib/libXau-1.0.12.tar.xz \
  74d0e4dfa3d39ad8939e99bda37f5967aba528211076828464d2777d477fc0fb
tar --extract --xz --file libXau-1.0.12.tar.xz
cd libXau-1.0.12
export PKG_CONFIG_LIBDIR="$work/sysroot/usr/lib/pkgconfig:$work/sysroot/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$work/sysroot"
unset PKG_CONFIG_PATH
LDFLAGS=-static ./configure --host=x86_64-linux-musl --prefix=/usr --disable-shared --enable-static
make -j2
install -m 0644 .libs/libXau.a "$work/sysroot/usr/lib/"

# Alpine's static libmount references libeconf, but mount.pc omits it and
# Alpine ships libeconf only as a shared library. Supply this private archive.
cd "$work"
fetch https://codeload.github.com/openSUSE/libeconf/tar.gz/refs/tags/v0.8.3 \
  c8855f08d64c06b57a5028a14eec6ddde5e35124f9718031d592b44d8e091f83 libeconf-0.8.3.tar.gz
tar --extract --gzip --file libeconf-0.8.3.tar.gz
cmake -S libeconf-0.8.3 -B econf-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS=-static
cmake --build econf-build --target econf -j2
install -m 0644 econf-build/lib/libeconf.a "$work/sysroot/usr/lib/"
install -m 0644 econf-build/libeconf.pc "$work/sysroot/usr/lib/pkgconfig/"
mount_pc="$work/sysroot/usr/lib/pkgconfig/mount.pc"
if ! grep -Eq '^Requires\.private:.*\blibeconf\b' "$mount_pc"; then
  sed -i '/^Requires\.private:/ s/$/ libeconf/' "$mount_pc"
fi
