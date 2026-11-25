# Copy to config.local.mk for a Linux native build.
# Build the official Unicorn 2.1.4 ARM backend here; see docs/LINUX.md.
UNICORN_ROOT = deps/unicorn
UNICORN_BUILD_DIR = $(UNICORN_ROOT)/build
UNICORN_LIB = $(UNICORN_BUILD_DIR)/libunicorn-static.a

# SDL2/GTK4 and optional Capstone use the system development packages.
# CC = gcc
# PKG_CONFIG = pkg-config
# PYTHON = python3
# NODE = node
# WASM_CMAKE = cmake
