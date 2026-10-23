#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export OPENPROSPERO_SDK=${OPENPROSPERO_SDK:-"$ROOT/../sdk/sdk"}
export OPENPROSPERO_LINK_FLAGS="--no-companion --title-heap"
WORK=${MIRROR_BUILD_ROOT:-"$HOME/.cache/mirrorpsx"}
PREFIX="$ROOT/build/ps5-deps"
mkdir -p "$WORK" "$PREFIX"
CC="$ROOT/tools/ps5-cc.sh"
chmod +x "$CC"
if [ ! -f "$PREFIX/lib/libcrypto.a" ]; then
    if [ ! -f "$WORK/openssl-3.5.2.tar.gz" ]; then curl -fL https://github.com/openssl/openssl/releases/download/openssl-3.5.2/openssl-3.5.2.tar.gz -o "$WORK/openssl-3.5.2.tar.gz"; fi
    [ -d "$WORK/openssl-3.5.2" ] || tar -xf "$WORK/openssl-3.5.2.tar.gz" -C "$WORK"
    (cd "$WORK/openssl-3.5.2"; CC="$CC" AR=llvm-ar-19 RANLIB=llvm-ranlib-19 ./Configure BSD-x86_64 no-shared no-tests no-dso no-engine no-module no-asm --prefix="$PREFIX" --libdir=lib; make -j6 build_libs; make install_dev)
fi
if [ ! -f "$PREFIX/.media-v2" ]; then
    # git archive provides LF sources even when Windows checked out CRLF scripts.
    if [ ! -f "$WORK/ffmpeg-source/configure" ]; then
        mkdir -p "$WORK/ffmpeg-source"
        git -C "$ROOT/reference/ffmpeg" archive HEAD | tar -x -C "$WORK/ffmpeg-source"
    fi
    mkdir -p "$WORK/ffmpeg"
    (cd "$WORK/ffmpeg"; "$WORK/ffmpeg-source/configure" \
      --prefix="$PREFIX" --arch=x86_64 --target-os=freebsd --enable-cross-compile \
      --cc="$CC" --ar=llvm-ar-19 --ranlib=llvm-ranlib-19 --disable-everything \
      --disable-programs --disable-doc --enable-network --disable-autodetect --disable-x86asm \
      --disable-shared --enable-static --enable-pic --enable-pthreads \
      --enable-decoder=h264,hevc,aac,alac,pcm_s16le,mp3float --enable-parser=h264,hevc,aac,mpegaudio --enable-swscale \
      --enable-avformat --enable-swresample --enable-demuxer=mov,matroska,mpegts,avi,mp3,wav \
      --enable-protocol=http,https,tcp,tls,crypto,udp --enable-openssl \
      --disable-avdevice --disable-avfilter \
      --extra-cflags="-femulated-tls -I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib"; make -j6; make install)
    touch "$PREFIX/.media-v2"
fi
if [ ! -f "$PREFIX/lib/libxml2.a" ]; then
    if [ ! -f "$WORK/libxml2-2.14.5.tar.xz" ]; then
        curl -fL https://download.gnome.org/sources/libxml2/2.14/libxml2-2.14.5.tar.xz -o "$WORK/libxml2-2.14.5.tar.xz"
    fi
    [ -d "$WORK/libxml2-2.14.5" ] || tar -xf "$WORK/libxml2-2.14.5.tar.xz" -C "$WORK"
    cmake -S "$WORK/libxml2-2.14.5" -B "$WORK/libxml2-build" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/ps5-toolchain.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLIBXML2_WITH_ICONV=OFF \
      -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_PYTHON=OFF \
      -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_HTTP=OFF
    cmake --build "$WORK/libxml2-build" -j6
    cmake --install "$WORK/libxml2-build"
fi
cp "$OPENPROSPERO_SDK/target/cxx/lib/libc.a" "$PREFIX/lib/libps5_libc.a"
# OpenProspero's application CRT already supplies gmtime_r. Keep the SDK archive
# unchanged and remove that conflicting object only from the local link copy.
llvm-ar-19 d "$PREFIX/lib/libps5_libc.a" gmtime.o
RADV_SOURCE=${MIRROR_RADV_ARCHIVE:-"$OPENPROSPERO_SDK/../build/ps5-mesa/build-release/src/amd/vulkan/libvulkan_radeon.a"}
cp "$RADV_SOURCE" "$PREFIX/lib/libvulkan_radeon.a"
printf 'PS5 dependencies ready: %s\n' "$PREFIX"
