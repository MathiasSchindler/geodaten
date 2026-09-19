CC := gcc-16
HOST_CXX ?= g++
NVCC ?= nvcc
OS := linux
ARCH := x86_64
.DEFAULT_GOAL := all
BUILD_DIR := build/freestanding-$(OS)-$(ARCH)
CUDA_BUILD_DIR := build/cuda-$(OS)-$(ARCH)
MAKE_JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(MAKE_JOBS)
endif
MACOS_CC ?= clang
MACOS_ARCH := aarch64
MACOS_BUILD_DIR := build/freestanding-macos-arm64
MACOS_SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)

# The default Linux build is intentionally freestanding/nolibc: no system
# headers, no C runtime startup files, no libc link, and no dynamic loader.
CFLAGS := -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fdata-sections -ffunction-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Isrc/shared -Isrc/shared/fontrender -Isrc/platform/linux -Isrc/platform/common -DNEWOS_DISABLE_STACK_GUARD_INIT -DFR_RASTER_DISABLE_SIMD
LDFLAGS := -nostdlib -static -no-pie -Wl,--gc-sections
HOST_CXXFLAGS := -std=c++17 -O2 -D_FILE_OFFSET_BITS=64 -Isrc/tools
NVCCFLAGS := -std=c++17 -O2 -D_FILE_OFFSET_BITS=64 -Isrc/tools
MACOS_CFLAGS := -target arm64-apple-macos11 -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fdata-sections -ffunction-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Isrc/shared -Isrc/shared/fontrender -Isrc/platform/macos -Isrc/platform/common -Isrc/arch/aarch64/macos -DNEWOS_DISABLE_STACK_GUARD_INIT -DFR_RASTER_DISABLE_SIMD
MACOS_LDFLAGS := -nostdlib -Wl,-syslibroot,$(MACOS_SDKROOT) -Wl,-e,_start -Wl,-dead_strip -lSystem

RUNTIME_SRCS := \
    src/arch/x86_64/linux/crt0.S \
    src/arch/x86_64/linux/syscall_stubs.S \
    src/platform/linux/io.c \
    src/shared/runtime/io.c \
    src/shared/runtime/memory.c \
    src/shared/runtime/parse.c \
    src/shared/runtime/string.c

FONTRENDER_SRCS := \
    src/shared/fontrender/fr_platform.c \
    src/shared/fontrender/fr_ttf.c \
    src/shared/fontrender/fr_raster.c \
    src/shared/fontrender/font_backend_truetype.c \
    src/shared/fontrender_runtime.c

MACOS_RUNTIME_SRCS := \
    src/arch/aarch64/macos/crt0.S \
    src/platform/macos/io.c \
    src/platform/macos/thread.c \
    src/platform/macos/time.c \
    src/shared/runtime/io.c \
    src/shared/runtime/memory.c \
    src/shared/runtime/parse.c \
    src/shared/runtime/string.c

MACOS_PBF_TO_RPACK_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/shared/osmrpack.c \
    src/tools/pbf_to_rpack.c

MACOS_RPACK_INFO_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/shared/osmrpack.c \
    src/tools/rpack_info.c

MACOS_RPACK_RENDER_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/shared/compression/crc32.c \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/shared/osmrpack.c \
    src/shared/simple_config.c \
    src/tools/rpack_render.c

MACOS_PBF_TO_RTE_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/pbf_to_rte.c

MACOS_RTE_INFO_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/tools/rte_info.c

MACOS_RTE_ROUTE_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/tools/rte_route.c

MACOS_TEST_THREAD_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/tools/test_thread.c

MACOS_OSM_POSTAL_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/osm_postal.c

MACOS_OSM_POSTAL_FIXTURE_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/tools/osm_postal_fixture.c

MACOS_LIBRE_HKDE_SRCS := \
	$(MACOS_RUNTIME_SRCS) \
    src/tools/libre_hkde.c

PBF_INFO_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/thread.c \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/pbf_info.c

OSM_LOOKUP_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/shared/compression/zlib.c \
    src/shared/osm_index.c \
    src/shared/pbf.c \
    src/tools/osm_lookup.c

OSM_ADDRESSES_SRCS := \
	$(RUNTIME_SRCS) \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/osm_addresses.c

OSM_BUILDINGS_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/shared/compression/zlib.c \
    src/shared/osm_index.c \
    src/shared/pbf.c \
    src/tools/osm_buildings.c

OSM_POSTAL_SRCS := \
	$(RUNTIME_SRCS) \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/osm_postal.c

OSM_POSTAL_FIXTURE_SRCS := \
	$(RUNTIME_SRCS) \
    src/tools/osm_postal_fixture.c

LIBRE_HKDE_SRCS := \
	$(RUNTIME_SRCS) \
    src/tools/libre_hkde.c

PBF_TO_RPACK_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/platform/linux/thread.c \
    src/platform/linux/time.c \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/shared/osmrpack.c \
    src/tools/pbf_to_rpack.c

RPACK_INFO_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/shared/osmrpack.c \
    src/tools/rpack_info.c

RPACK_RENDER_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/platform/linux/time.c \
    src/shared/compression/crc32.c \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/shared/osmrpack.c \
    src/shared/simple_config.c \
    src/tools/rpack_render.c

PBF_TO_RTE_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/thread.c \
    src/platform/linux/time.c \
    src/shared/compression/zlib.c \
    src/shared/pbf.c \
    src/tools/pbf_to_rte.c

RTE_INFO_SRCS := \
	$(RUNTIME_SRCS) \
    src/tools/rte_info.c

RTE_ROUTE_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
    src/platform/linux/identity.c \
    src/platform/linux/process.c \
    src/platform/linux/time.c \
    src/tools/rte_route.c

TEST_THREAD_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/thread.c \
    src/tools/test_thread.c

TEST_FONT_SRCS := \
	$(RUNTIME_SRCS) \
    src/platform/linux/fs.c \
	$(FONTRENDER_SRCS) \
    src/tools/test_font.c

LINUX_TOOLS := \
	$(BUILD_DIR)/pbf-info \
	$(BUILD_DIR)/osm-lookup \
	$(BUILD_DIR)/osm-addresses \
	$(BUILD_DIR)/osm-buildings \
	$(BUILD_DIR)/osm-postal \
	$(BUILD_DIR)/osm-postal-fixture \
	$(BUILD_DIR)/libre-hkde \
	$(BUILD_DIR)/pbf-to-rpack \
	$(BUILD_DIR)/rpack-info \
	$(BUILD_DIR)/rpack-render \
	$(BUILD_DIR)/pbf-to-rte \
	$(BUILD_DIR)/rte-info \
	$(BUILD_DIR)/rte-route \
	$(BUILD_DIR)/test-thread \
	$(BUILD_DIR)/test-font

.PHONY: all clean check-static test-thread macos-rpack-tools macos-test-thread rte-gpu-tools \
	postal-tools macos-postal-tools test-postal macos-test-postal \
	libre-hkde-tools macos-libre-hkde-tools test-libre-hkde macos-test-libre-hkde

all: $(LINUX_TOOLS)

check-static: all
	@set -e; \
	for tool in $(LINUX_TOOLS); do \
		if readelf -l "$$tool" | grep -q 'INTERP'; then echo "$$tool: has dynamic interpreter"; exit 1; fi; \
		if readelf -d "$$tool" 2>/dev/null | grep -q 'NEEDED'; then echo "$$tool: has shared library dependency"; exit 1; fi; \
		if nm -u "$$tool" 2>/dev/null | grep -q .; then echo "$$tool: has undefined symbols"; exit 1; fi; \
	done; \
	echo "all Linux tools are static freestanding ELF binaries"

test-thread: $(BUILD_DIR)/test-thread

macos-rpack-tools: $(MACOS_BUILD_DIR)/pbf-to-rpack $(MACOS_BUILD_DIR)/rpack-info $(MACOS_BUILD_DIR)/rpack-render $(MACOS_BUILD_DIR)/pbf-to-rte $(MACOS_BUILD_DIR)/rte-info $(MACOS_BUILD_DIR)/rte-route

macos-test-thread: $(MACOS_BUILD_DIR)/test-thread

postal-tools: $(BUILD_DIR)/osm-postal $(BUILD_DIR)/osm-postal-fixture

macos-postal-tools: $(MACOS_BUILD_DIR)/osm-postal $(MACOS_BUILD_DIR)/osm-postal-fixture

libre-hkde-tools: $(BUILD_DIR)/libre-hkde

macos-libre-hkde-tools: $(MACOS_BUILD_DIR)/libre-hkde

test-libre-hkde: libre-hkde-tools
	@sh tests/run_libre_hkde_test.sh $(BUILD_DIR)

macos-test-libre-hkde: macos-libre-hkde-tools
	@sh tests/run_libre_hkde_test.sh $(MACOS_BUILD_DIR)

test-postal: postal-tools
	@sh tests/run_osm_postal_test.sh $(BUILD_DIR)

macos-test-postal: macos-postal-tools
	@sh tests/run_osm_postal_test.sh $(MACOS_BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(MACOS_BUILD_DIR):
	mkdir -p $@

$(CUDA_BUILD_DIR):
	mkdir -p $@

rte-gpu-tools: $(CUDA_BUILD_DIR)/rte-to-rtegpu $(CUDA_BUILD_DIR)/rte-gpu-info $(CUDA_BUILD_DIR)/rte-gpu-route

$(CUDA_BUILD_DIR)/rte-to-rtegpu: src/tools/rte_to_rtegpu.cpp src/tools/rtegpu_common.h | $(CUDA_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) src/tools/rte_to_rtegpu.cpp -o $@

$(CUDA_BUILD_DIR)/rte-gpu-info: src/tools/rte_gpu_info.cpp src/tools/rtegpu_common.h | $(CUDA_BUILD_DIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) src/tools/rte_gpu_info.cpp -o $@

$(CUDA_BUILD_DIR)/rte-gpu-route: src/tools/rte_gpu_route.cu src/tools/rtegpu_common.h | $(CUDA_BUILD_DIR)
	$(NVCC) $(NVCCFLAGS) src/tools/rte_gpu_route.cu -o $@

$(BUILD_DIR)/pbf-info: $(PBF_INFO_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(PBF_INFO_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/osm-lookup: $(OSM_LOOKUP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OSM_LOOKUP_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/osm-addresses: $(OSM_ADDRESSES_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OSM_ADDRESSES_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/osm-buildings: $(OSM_BUILDINGS_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OSM_BUILDINGS_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/osm-postal: $(OSM_POSTAL_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OSM_POSTAL_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/osm-postal-fixture: $(OSM_POSTAL_FIXTURE_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OSM_POSTAL_FIXTURE_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/libre-hkde: $(LIBRE_HKDE_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LIBRE_HKDE_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/pbf-to-rpack: $(PBF_TO_RPACK_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(PBF_TO_RPACK_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/rpack-info: $(RPACK_INFO_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RPACK_INFO_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/rpack-render: $(RPACK_RENDER_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RPACK_RENDER_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/pbf-to-rte: $(PBF_TO_RTE_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(PBF_TO_RTE_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/rte-info: $(RTE_INFO_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RTE_INFO_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/rte-route: $(RTE_ROUTE_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RTE_ROUTE_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/test-thread: $(TEST_THREAD_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_THREAD_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR)/test-font: $(TEST_FONT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_FONT_SRCS) $(LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/pbf-to-rpack: $(MACOS_PBF_TO_RPACK_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_PBF_TO_RPACK_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/rpack-info: $(MACOS_RPACK_INFO_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_RPACK_INFO_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/rpack-render: $(MACOS_RPACK_RENDER_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_RPACK_RENDER_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/pbf-to-rte: $(MACOS_PBF_TO_RTE_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_PBF_TO_RTE_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/rte-info: $(MACOS_RTE_INFO_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_RTE_INFO_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/rte-route: $(MACOS_RTE_ROUTE_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_RTE_ROUTE_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/test-thread: $(MACOS_TEST_THREAD_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_TEST_THREAD_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/osm-postal: $(MACOS_OSM_POSTAL_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_OSM_POSTAL_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/osm-postal-fixture: $(MACOS_OSM_POSTAL_FIXTURE_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_OSM_POSTAL_FIXTURE_SRCS) $(MACOS_LDFLAGS) -o $@

$(MACOS_BUILD_DIR)/libre-hkde: $(MACOS_LIBRE_HKDE_SRCS) | $(MACOS_BUILD_DIR)
	$(MACOS_CC) $(MACOS_CFLAGS) $(MACOS_LIBRE_HKDE_SRCS) $(MACOS_LDFLAGS) -o $@

clean:
	rm -rf build
