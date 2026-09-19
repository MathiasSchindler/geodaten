#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
XZ_VERSION=5.8.1
XZ_URL="https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/xz-${XZ_VERSION}.tar.xz"
TMPDIR=${TMPDIR:-/tmp}
WORK_DIR="${TMPDIR%/}/pbf-parser-xz-wasm-${XZ_VERSION}"
SRC_DIR="$WORK_DIR/src"

cd "$ROOT_DIR"
mkdir -p browser/wasm "$WORK_DIR"

if [ ! -d "$SRC_DIR" ]; then
  rm -rf "$SRC_DIR"
  mkdir -p "$SRC_DIR"
  curl -L --fail "$XZ_URL" -o "$WORK_DIR/xz.tar.xz"
  tar -xf "$WORK_DIR/xz.tar.xz" -C "$SRC_DIR" --strip-components=1
fi

if [ ! -f "$SRC_DIR/src/liblzma/.libs/liblzma.a" ]; then
  (cd "$SRC_DIR" && \
    emconfigure ./configure \
      --host=wasm32-unknown-emscripten \
      --disable-shared \
      --enable-static \
      --disable-xz \
      --disable-xzdec \
      --disable-lzmadec \
      --disable-lzmainfo \
      --disable-scripts \
      --disable-doc \
      --disable-nls \
      --disable-threads \
      --enable-checks=crc32,crc64,sha256 \
      --enable-encoders=lzma1,lzma2 \
      --enable-decoders=lzma1,lzma2)
  emmake make -j2 -C "$SRC_DIR/src/liblzma" liblzma.la
fi

emcc -O2 \
  -I"$SRC_DIR/src/liblzma/api" \
  browser/xzdec_emscripten.c \
  "$SRC_DIR/src/liblzma/.libs/liblzma.a" \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createXzDecodeModule \
  -sFORCE_FILESYSTEM=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=536870912 \
  -sEXPORTED_RUNTIME_METHODS='["FS","callMain"]' \
  -sEXIT_RUNTIME=0 \
  -o browser/wasm/xzdec.js
