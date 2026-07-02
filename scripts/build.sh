#!/bin/sh
#
# Build the seL4 system image (wrapped in OpenSBI) and the TinyEmu emulator.
#
# Works on both Linux and macOS: toolchain differences are handled by the
# CMake build itself (see ../opensbi.cmake); this script only prepares the
# host environment (Python venv, keg-only Homebrew tools).
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")

# The elfloader build needs GNU cpio ('cpio --append'); Homebrew installs it
# keg-only, so put it in front of macOS' BSD cpio if it is installed.
if command -v brew > /dev/null 2>&1; then
    cpio_prefix=$(brew --prefix cpio 2> /dev/null || true)
    if [ -n "$cpio_prefix" ] && [ -d "$cpio_prefix/bin" ]; then
        PATH="$cpio_prefix/bin:$PATH"
    fi
fi

# The kernel build needs its Python tools (sel4-deps); use the repo venv if
# there is one.
if [ -f "$repo_root/.venv/bin/activate" ]; then
    . "$repo_root/.venv/bin/activate"
fi

mkdir -p "$repo_root/build"
cd "$repo_root/build"
if [ ! -f build.ninja ]; then
    "$repo_root/init-build.sh"
fi
ninja

make -C "$repo_root/tools/TinyEmu"
