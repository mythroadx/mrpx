CC := gcc
.DEFAULT_GOAL := all
BUILD_DIR ?= build
PKG_CONFIG ?= pkg-config
PWSH ?= pwsh
GO ?= go
NPM ?= $(if $(filter Windows_NT,$(OS)),npm.cmd,npm)
JOBS ?= 4
VERSION ?=
PLATFORM ?= $(if $(filter Windows_NT,$(OS)),windows,linux)
RUNTIME_DIR ?= bin
PORT ?= 18766
BROWSER_URL ?= http://127.0.0.1:$(PORT)
MRP_SOURCE ?=
MRP_DOWNLOAD_BASE ?= https://github.com/vmrp/vmrp.github.io/tree/master/public/mrps
ifeq ($(OS),Windows_NT)
EXE_SUFFIX := .exe
UNICORN_ROOT ?= deps/unicorn
SDL2_ROOT ?= deps/SDL2-2.32.10/x86_64-w64-mingw32
ARCH_FLAGS ?= -m64
PYTHON ?= python
CAPSTONE_ROOT ?= deps/capstone
CAPSTONE_LIB ?= $(CAPSTONE_ROOT)/build/libcapstone.a
else
ifneq ($(shell uname -s),Linux)
$(error Supported native build platforms are Windows and Linux)
endif
EXE_SUFFIX :=
UNICORN_ROOT ?= deps/unicorn
ARCH_FLAGS ?=
PYTHON ?= python3
CAPSTONE_ROOT ?= /usr
CAPSTONE_LIB ?= $(shell $(PKG_CONFIG) --variable=libdir capstone)/libcapstone.so
endif
UNICORN_BUILD_DIR ?= $(UNICORN_ROOT)/build
UNICORN_LIB ?= $(UNICORN_BUILD_DIR)/libunicorn-static.a
CAPSTONE_INCLUDE ?= $(CAPSTONE_ROOT)/include
NODE ?= $(if $(EMSDK_NODE),"$(EMSDK_NODE)",node)

-include config.local.mk

UNICORN_LINK = -Wl,--start-group "$(UNICORN_LIB)" \
	$(if $(filter %libunicorn-static.a,$(subst \,/,$(UNICORN_LIB))),"$(UNICORN_BUILD_DIR)/libarm-softmmu.a" "$(UNICORN_BUILD_DIR)/libunicorn-common.a") \
	-Wl,--end-group

ifeq ($(OS),Windows_NT)
PLATFORM_CFLAGS := -I"$(SDL2_ROOT)/include"
SDL_LIBS := -L"$(SDL2_ROOT)/lib" -lSDL2
NETWORK_LIBS := -lws2_32
CAPTURE_SOURCES = $(SRC_DIR)/capture_windows.c
CAPTURE_LIBS := -lmfuuid -lwindowscodecs -lole32 -luuid
CONTROLS_LIBS := $(SDL_LIBS) -luser32 -lgdi32 -lcomdlg32 $(CAPTURE_LIBS)
PLATFORM_LIBS := -lmingw32 -lSDL2main $(CONTROLS_LIBS)
TEST_RUNTIME_FILES = $(TEST_DIR)/SDL2.dll
else
PLATFORM_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2 "gtk4 >= 4.10") -DVMRP_GTK
SDL_LIBS = $(shell $(PKG_CONFIG) --libs sdl2)
NETWORK_LIBS :=
CONTROLS_LIBS = $(SDL_LIBS) $(shell $(PKG_CONFIG) --libs "gtk4 >= 4.10")
PLATFORM_LIBS = $(CONTROLS_LIBS)
TEST_RUNTIME_FILES :=
endif

SRC_DIR := src
INCLUDE_DIR := include
include wasm/config.mk
NATIVE_CMAKE ?= $(WASM_CMAKE)

export VERSION PLATFORM UNICORN_ROOT SDL2_ROOT JOBS NATIVE_CMAKE WASM_CMAKE NPM
export WASM_UNICORN_ROOT WASM_UNICORN_BUILD RUNTIME_DIR BROWSER_URL
export MRP_SOURCE MRP_DOWNLOAD_BASE PYTHON

COMMON_SOURCES := bridge.c controls.c display.c edit.c dispatch.c fileLib.c memory.c network.c rbtree.c utils.c vmrp.c main.c
ifeq ($(OS),Windows_NT)
COMMON_SOURCES += capture_windows.c
endif
BUILD_CONFIG := release
TARGET := bin/main$(EXE_SUFFIX)
CFLAGS = -g -Wall -std=gnu11 -DNETWORK_SUPPORT -DVMRP \
	-I"$(INCLUDE_DIR)" -I"$(UNICORN_ROOT)/include" -I"$(UNICORN_BUILD_DIR)" \
	$(PLATFORM_CFLAGS)
LIBS = $(UNICORN_LINK) -lpthread -lm $(NETWORK_LIBS) $(PLATFORM_LIBS)

ifeq ($(DEBUG),1)
BUILD_CONFIG := debug
TARGET := bin/main-debug$(EXE_SUFFIX)
COMMON_SOURCES += debug.c
CFLAGS += -DDEBUG -I"$(CAPSTONE_INCLUDE)"
LIBS += "$(CAPSTONE_LIB)"
endif

OBJ_DIR := $(BUILD_DIR)/$(BUILD_CONFIG)/obj
OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(COMMON_SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

.PHONY: all help main debug de wasm native-deps web-deps package serve test test-build test-controls test-capture test-startup test-web-controls test-web-audio test-web-catalog test-catalog catalog test-go test-package test-browser prepare-web-audio prepare-runtime prepare-web test-mrp test-mrp-runner clean
.DELETE_ON_ERROR:

-include $(DEPFILES)

all: main

help:
	@printf '%s\n' \
		'mrpx project commands (run from the repository root)' \
		'  make main | debug | de | wasm             Build; default is main' \
		'  make native-deps JOBS=4                   Prepare native SDKs explicitly' \
		'    UNICORN_ROOT=<path> SDL2_ROOT=<path> NATIVE_CMAKE=<path>' \
		'  make web-deps                            Install locked npm dependencies' \
		'  make prepare-web-audio | wasm-unicorn    Prepare WASM inputs; activate SDK first' \
		'  make prepare-runtime RUNTIME_DIR=<path>  Copy seeds; preserve app data' \
		'  make prepare-web                        Stage frontend and runtime' \
		'  make package VERSION=vX.Y.Z PLATFORM=windows|linux|wasm' \
		'    Package existing builds; native platform is the default' \
		'  make test | test-controls | test-capture | test-startup | test-mrp | test-build' \
		'  make test-web-controls | test-web-audio | test-go | test-package' \
		'  make catalog MRP_SOURCE=<external-directory> JOBS=4' \
		'    Generate JSON/PNG only; downloads point to MRP_DOWNLOAD_BASE' \
		'  make test-catalog | test-web-catalog     Test extraction, capture and catalog UI logic' \
		'  make test-wasm-unicorn                   Test prepared Unicorn smoke build' \
		'  make serve PORT=18766                    Serve localhost in this terminal' \
		'  make test-browser BROWSER_URL=http://127.0.0.1:18766' \
		'    Requires a running project HTTP server and built WASM' \
		'  make clean                              Preserve runtime data and native SDKs'

main: $(TARGET)

TEST_DIR := $(BUILD_DIR)/tests
TEST_TARGETS := $(addprefix $(TEST_DIR)/,$(addsuffix $(EXE_SUFFIX),memory-tests network-tests dispatch-tests guest-tests edit-tests display-tests))
TEST_CFLAGS = $(CFLAGS) $(ARCH_FLAGS) -UNDEBUG -DSDL_MAIN_HANDLED -ffunction-sections -fdata-sections
TEST_HEADERS := $(wildcard $(INCLUDE_DIR)/vmrp/*.h)

test: $(TEST_TARGETS) $(TEST_RUNTIME_FILES)
	"$(TEST_DIR)/memory-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/network-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/dispatch-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/guest-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/edit-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/display-tests$(EXE_SUFFIX)"

$(TEST_DIR)/display-tests$(EXE_SUFFIX): tests/display_tests.c $(SRC_DIR)/display.c $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/display_tests.c $(SRC_DIR)/display.c $(SDL_LIBS)

$(TEST_DIR)/edit-tests$(EXE_SUFFIX): tests/edit_tests.c $(SRC_DIR)/edit.c $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/edit_tests.c $(SRC_DIR)/edit.c $(SDL_LIBS)

test-controls: $(TEST_DIR)/controls-tests$(EXE_SUFFIX) $(TEST_RUNTIME_FILES)
	"$(TEST_DIR)/controls-tests$(EXE_SUFFIX)"

$(TEST_DIR)/controls-tests$(EXE_SUFFIX): tests/controls_tests.c $(SRC_DIR)/controls.c $(SRC_DIR)/edit.c $(SRC_DIR)/display.c $(CAPTURE_SOURCES) $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/controls_tests.c $(SRC_DIR)/edit.c $(SRC_DIR)/display.c $(CAPTURE_SOURCES) $(CONTROLS_LIBS)

ifeq ($(OS),Windows_NT)
test-capture: $(TEST_DIR)/capture-tests$(EXE_SUFFIX) $(TEST_DIR)/controls-tests$(EXE_SUFFIX) $(TEST_RUNTIME_FILES)
	"$(TEST_DIR)/capture-tests$(EXE_SUFFIX)"
	"$(TEST_DIR)/controls-tests$(EXE_SUFFIX)" --capture

$(TEST_DIR)/capture-tests$(EXE_SUFFIX): tests/capture_tests.c $(CAPTURE_SOURCES) $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/capture_tests.c $(CAPTURE_SOURCES) $(SDL_LIBS) $(CAPTURE_LIBS) -lmfplat -lmfreadwrite
else
test-capture:
	@echo "Capture tests require Windows Media Foundation." >&2
	@exit 1
endif

test-web-controls:
	$(NODE) --test tests/web_controls_tests.js

test-web-audio:
	$(NODE) --test tests/web_audio_tests.js

test-build:
	"$(PWSH)" -NoProfile -File tests/build_script_tests.ps1

test-go:
	"$(GO)" -C webserver test ./...

test-package:
	$(PYTHON) -m unittest discover -s tests -p test_package_zip.py

test-browser:
	$(NODE) tests/web_browser_tests.mjs

serve:
	$(PYTHON) -m http.server "$(PORT)" --bind 127.0.0.1

web-deps:
	"$(PWSH)" -NoProfile -Command '& $$env:NPM ci'

native-deps:
	"$(PWSH)" -NoProfile -Command '& ./scripts/build.ps1 native-deps -CMake $$env:NATIVE_CMAKE -Jobs $$env:JOBS -UnicornRoot $$env:UNICORN_ROOT -SDL2Root $$env:SDL2_ROOT'

package:
	"$(PWSH)" -NoProfile -Command '& ./scripts/build.ps1 package -Version $$env:VERSION -Platform $$env:PLATFORM -UnicornRoot $$env:UNICORN_ROOT -SDL2Root $$env:SDL2_ROOT'

prepare-web-audio:
	"$(PWSH)" -NoProfile -File scripts/build.ps1 audio

test-mrp-runner:
	$(PYTHON) -m unittest discover -s tests -p test_mrp_regression.py

test-mrp: $(TARGET) test-mrp-runner
	$(PYTHON) tools/mrp_regression.py --simulator "$(TARGET)" --output "$(BUILD_DIR)/mrp-regression"

test-startup: $(TARGET)
	$(PYTHON) tests/native_startup_tests.py --simulator "$(TARGET)"

$(TEST_DIR)/memory-tests$(EXE_SUFFIX): tests/memory_tests.c $(SRC_DIR)/memory.c $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/memory_tests.c $(SDL_LIBS)

$(TEST_DIR)/network-tests$(EXE_SUFFIX): tests/network_tests.c $(SRC_DIR)/network.c $(SRC_DIR)/dispatch.c $(SRC_DIR)/utils.c $(SRC_DIR)/rbtree.c $(TEST_HEADERS) | $(TEST_DIR)
	# PE unwind tables otherwise retain unrelated utility functions during section GC.
	$(CC) $(TEST_CFLAGS) -fno-asynchronous-unwind-tables -Wl,--gc-sections -o "$@" tests/network_tests.c $(SRC_DIR)/dispatch.c $(SRC_DIR)/utils.c $(SRC_DIR)/rbtree.c -lpthread $(NETWORK_LIBS) $(SDL_LIBS)

$(TEST_DIR)/dispatch-tests$(EXE_SUFFIX): tests/dispatch_tests.c $(SRC_DIR)/dispatch.c $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/dispatch_tests.c $(SRC_DIR)/dispatch.c $(SDL_LIBS)

$(TEST_DIR)/guest-tests$(EXE_SUFFIX): tests/guest_tests.c $(SRC_DIR)/bridge.c $(SRC_DIR)/vmrp.c $(addprefix $(SRC_DIR)/,memory.c network.c dispatch.c utils.c rbtree.c fileLib.c) $(TEST_HEADERS) | $(TEST_DIR)
	$(CC) $(TEST_CFLAGS) -o "$@" tests/guest_tests.c $(addprefix $(SRC_DIR)/,memory.c network.c dispatch.c utils.c rbtree.c fileLib.c) $(UNICORN_LINK) -lpthread $(NETWORK_LIBS) -lm $(SDL_LIBS)

$(TEST_DIR):
	mkdir -p "$@"

ifeq ($(OS),Windows_NT)
$(TEST_DIR)/SDL2.dll: $(SDL2_ROOT)/bin/SDL2.dll | $(TEST_DIR)
	cp "$<" "$@"
endif

debug:
	$(MAKE) main DEBUG=1

de:
	$(CC) $(ARCH_FLAGS) -I"$(CAPSTONE_INCLUDE)" tools/de.c "$(CAPSTONE_LIB)" -o tools/de$(EXE_SUFFIX)

prepare-runtime:
	"$(PWSH)" -NoProfile -Command '& ./scripts/build.ps1 runtime -Destination $$env:RUNTIME_DIR'

prepare-web:
	"$(PWSH)" -NoProfile -File scripts/build.ps1 web

wasm: prepare-web $(WASM_TARGET)

.PHONY: wasm-unicorn test-wasm-unicorn
wasm-unicorn:
	"$(PWSH)" -NoProfile -Command '& ./scripts/build.ps1 unicorn -CMake $$env:WASM_CMAKE -SourceDirectory $$env:WASM_UNICORN_ROOT -EngineDirectory $$env:WASM_UNICORN_BUILD -Jobs $$env:JOBS'

test-wasm-unicorn:
	$(NODE) tests/unicorn_wasm_tests.js build/unicorn-wasm/smoke.js

$(WASM_UNICORN_LIBS):
	$(error Missing WASM Unicorn 2.1.4 backend; activate Emscripten and run make wasm-unicorn)

$(WASM_TARGET): $(WASM_SOURCES) $(WASM_HEADERS) $(WASM_UNICORN_LIBS) wasm/config.mk
	mkdir -p "$(dir $@)"
	$(EMCC) $(WASM_CFLAGS) $(WASM_SOURCES) -Wl,--start-group $(foreach lib,$(WASM_UNICORN_LIBS),"$(lib)") -Wl,--end-group $(WASM_SETTINGS) -o "$@"

$(TARGET): $(OBJECTS) | bin prepare-runtime
	$(CC) $(CFLAGS) $(ARCH_FLAGS) -o "$@" $(OBJECTS) $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(ARCH_FLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(OBJ_DIR):
	mkdir -p "$@"

bin:
	mkdir -p "$@"

clean:
	rm -rf "$(BUILD_DIR)/release" "$(BUILD_DIR)/debug" "$(BUILD_DIR)/tests" "$(BUILD_DIR)/mrp-regression"
	rm -rf build/web build/unicorn-wasm build/web-audio-downloads
	rm -f bin/main.exe bin/main-debug.exe tools/de.exe bin/main bin/main-debug tools/de
	rm -f "$(WASM_TARGET)" "$(WASM_TARGET:.js=.wasm)"
	rm -f *.o



catalog:
	"$(PWSH)" -NoProfile -Command '& $$env:PYTHON ./tools/mrp_to_markdown.py $$env:MRP_SOURCE --catalog --capture --resume-capture --capture-delay 8000 --workers $$env:JOBS --download-base-url $$env:MRP_DOWNLOAD_BASE --output ./wasm/web/catalog'

test-catalog:
	$(PYTHON) -m unittest discover -s tests -p test_mrp_catalog.py

test-web-catalog:
	$(NODE) --test tests/web_catalog_tests.js

