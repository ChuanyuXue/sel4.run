#!/bin/sh
#
# Build the browser (WebAssembly) version of TinyEmu and assemble a static
# site under build/web/ that boots the seL4 system image in a web terminal.
#
# Needs emscripten (macOS: 'brew install emscripten') and a previously built
# system image (scripts/build.sh).
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")

if ! command -v emcc > /dev/null 2>&1; then
    echo "error: emcc not found; install emscripten first" >&2
    echo "  (macOS: brew install emscripten)" >&2
    exit 1
fi

image="$repo_root/build/images/hello-image-riscv-spike.bin"
if [ ! -f "$image" ]; then
    echo "error: $image not found; run scripts/build.sh first" >&2
    exit 1
fi

make -C "$repo_root/tools/TinyEmu" -f Makefile.js

dist="$repo_root/build/web"
mkdir -p "$dist"
cp "$repo_root/tools/TinyEmu/js/riscvemu64-wasm.js" \
   "$repo_root/tools/TinyEmu/js/riscvemu64-wasm.wasm" \
   "$script_dir/../web/index.html" \
   "$repo_root/build/images/tinyemu.cfg" \
   "$image" \
   "$dist/"

echo "Static site assembled in $dist"
echo "Serve it with: sel4.run/scripts/run-web.sh"
