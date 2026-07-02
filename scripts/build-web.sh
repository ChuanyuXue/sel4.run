#!/bin/sh
#
# Assemble the sel4.run lab as a static site under build/web/:
#   - TinyEmu compiled to wasm (emulator)
#   - clang/lld/llvm-objcopy compiled to wasm (in-browser toolchain)
#   - sysroot.tar.gz (headers, libs, prebuilt objects + build recipes)
#   - the lab page (editor, terminal, workers, examples)
#
# Needs emscripten and a native build first (scripts/build.sh). The wasm
# toolchain is expected in build/llvm-wasm (see README for the LLVM build).
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")
web_src="$script_dir/../web"
dist="$repo_root/build/web"

if ! command -v emcc > /dev/null 2>&1; then
    echo "error: emcc not found; install emscripten first" >&2
    exit 1
fi
if [ ! -f "$repo_root/build/build.ninja" ]; then
    echo "error: build/ not configured; run scripts/build.sh first" >&2
    exit 1
fi

make -C "$repo_root/tools/TinyEmu" -f Makefile.js

python3 "$script_dir/build-sysroot.py" "$dist/sysroot.tar.gz"

mkdir -p "$dist/examples"
cp "$web_src/index.html" "$web_src/app.js" \
   "$web_src/compiler-worker.js" "$web_src/emulator-worker.js" \
   "$repo_root/tools/TinyEmu/js/riscvemu64-wasm.js" \
   "$repo_root/tools/TinyEmu/js/riscvemu64-wasm.wasm" \
   "$dist/"
cp "$web_src"/examples/* "$dist/examples/"

toolchain_ok=1
for tool in clang lld llvm-objcopy; do
    js="$repo_root/build/llvm-wasm/bin/$tool.js"
    if [ -f "$js" ]; then
        cp "${js%.js}.wasm" "$dist/"
        # The tools are built with memory growth, which makes the wasm heap a
        # resizable ArrayBuffer; Chrome's TextDecoder refuses views on those.
        # Decode from a copy instead.
        sed 's|UTF8Decoder.decode(heapOrArray.subarray(idx,endPtr))|UTF8Decoder.decode(heapOrArray.slice(idx,endPtr))|' \
            "$js" > "$dist/$tool.js"
    else
        echo "warning: $js not built yet; Run will not work" >&2
        toolchain_ok=0
    fi
done

echo "Static site assembled in $dist (toolchain: $([ $toolchain_ok = 1 ] && echo ok || echo MISSING))"
echo "Serve it with: sel4.run/scripts/run-web.sh"
