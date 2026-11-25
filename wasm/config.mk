EMCC ?= emcc
WASM_CMAKE ?= cmake
WASM_UNICORN_ROOT ?= build/unicorn-wasm/source
WASM_UNICORN_BUILD ?= build/unicorn-wasm/engine
WASM_UNICORN_LIBS := $(WASM_UNICORN_BUILD)/libunicorn.a \
	$(WASM_UNICORN_BUILD)/libarm-softmmu.a $(WASM_UNICORN_BUILD)/libunicorn-common.a
WASM_TARGET ?= build/web/mrpx.js

WASM_SOURCES := \
	src/main.c \
	src/controls.c \
	src/display.c \
	src/edit.c \
	src/dispatch.c \
	src/fileLib.c \
	src/network.c \
	src/vmrp.c \
	src/utils.c \
	src/rbtree.c \
	src/bridge.c \
	src/memory.c

WASM_CFLAGS := -Os -Wall -DVMRP -Iinclude -I"$(WASM_UNICORN_ROOT)/include"
WASM_SETTINGS := \
	-s EXPORTED_FUNCTIONS="['_main', '_malloc', '_free']" \
	-s EXPORTED_RUNTIME_METHODS="['HEAPU8', 'HEAPU16']" \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s ALLOW_TABLE_GROWTH=1 \
	-s USE_SDL=2 \
	-s USE_ZLIB=1 \
	-s WASM=1 \
	-s FORCE_FILESYSTEM=1

WASM_HEADERS := $(wildcard include/vmrp/*.h) \
	$(wildcard $(WASM_UNICORN_ROOT)/include/unicorn/*.h)