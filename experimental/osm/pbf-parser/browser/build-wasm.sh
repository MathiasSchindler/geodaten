#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

mkdir -p browser/wasm

COMMON_FLAGS="-O2 -ffunction-sections -fdata-sections -Isrc/shared -Isrc/platform/common -Ibrowser -DNEWOS_DISABLE_STACK_GUARD_INIT -D__EMSCRIPTEN_BROWSER__"
COMMON_LINK="-Wl,--gc-sections -sMODULARIZE=1 -sFORCE_FILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912 -sEXPORTED_RUNTIME_METHODS=[\"FS\",\"callMain\"] -sEXIT_RUNTIME=0"
RUNTIME_SRCS="browser/platform_emscripten.c src/shared/runtime/io.c src/shared/runtime/memory.c src/shared/runtime/parse.c src/shared/runtime/string.c"

emcc $COMMON_FLAGS $RUNTIME_SRCS src/tools/rte_route.c \
  $COMMON_LINK -sEXPORT_NAME=createRteRouteModule \
  -o browser/wasm/rte-route.js

emcc $COMMON_FLAGS $RUNTIME_SRCS \
  src/shared/compression/crc32.c src/shared/compression/zlib.c \
  src/shared/pbf.c src/shared/osmrpack.c src/shared/simple_config.c \
  src/tools/rpack_render.c \
  $COMMON_LINK -sEXPORT_NAME=createRpackRenderModule \
  -o browser/wasm/rpack-render.js

sh browser/build-xz-wasm.sh