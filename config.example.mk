# Copy to config.local.mk. Paths here use GNU Make's forward-slash syntax.
# The documented reference setup extracts the official Windows x64 wheel here.
UNICORN_ROOT = deps/unicorn
UNICORN_BUILD_DIR = $(UNICORN_ROOT)/lib
UNICORN_LIB = $(UNICORN_BUILD_DIR)/unicorn.dll
SDL2_ROOT = deps/SDL2-2.32.10/x86_64-w64-mingw32

# Override if native Windows CMake is not first on PATH. Do not quote the value.
# WASM_CMAKE = C:/Program Files/CMake/bin/cmake.exe

# Optional source-built desktop libraries instead of the DLL setup above:
# UNICORN_ROOT = deps/unicorn
# UNICORN_BUILD_DIR = $(UNICORN_ROOT)/build
# UNICORN_LIB = $(UNICORN_BUILD_DIR)/libunicorn-static.a
# CAPSTONE_ROOT = deps/capstone

# Optional command overrides:
# CC = gcc
# PYTHON = python
# NODE = node
# EMCC = emcc
