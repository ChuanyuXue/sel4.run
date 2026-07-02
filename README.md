# sel4.run

A seL4 API lab in the browser: edit real seL4 root-task C code, compile it
with a WebAssembly build of clang/lld, and boot the resulting image on a
WebAssembly build of TinyEmu (RISC-V64) -- entirely client-side. Ten guided
examples walk the seL4 tutorial topics: BootInfo, capabilities, untyped
retype, IPC, notifications, threads, frame mapping and fault handling.

## Checkout

```sh
repo init -u git@github.com:ChuanyuXue/sel4.run.git -m manifest.xml
repo sync
```

## Build and run (native)

```sh
sel4.run/scripts/build.sh     # CMake build: kernel, libs, image + temu
sel4.run/scripts/run.sh       # boot the image in native TinyEmu
```

## Build and run (the browser lab)

Needs emscripten (macOS: `brew install emscripten`) and a native build first.

The in-browser toolchain is LLVM compiled to wasm (one-time, ~1-2 h):

```sh
cd build && tar xf <llvm-project-22.1.8.src.tar.xz>   # match Homebrew LLVM
mkdir llvm-wasm && cd llvm-wasm
emcmake cmake ../llvm-project-22.1.8.src/llvm -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TARGETS_TO_BUILD=RISCV \
  -DLLVM_NATIVE_TOOL_DIR=/opt/homebrew/opt/llvm/bin \
  -DLLVM_ENABLE_THREADS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF -DLLVM_ENABLE_BINDINGS=OFF \
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-sMODULARIZE=1 -sEXPORT_NAME=LLVMTool -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=8388608 -sEXPORTED_RUNTIME_METHODS=FS,callMain -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sENVIRONMENT=web,worker,node"
ninja clang lld llvm-objcopy
```

Then assemble and serve the lab:

```sh
sel4.run/scripts/build-web.sh   # TinyEmu wasm + sysroot + toolchain + page
sel4.run/scripts/run-web.sh     # open http://localhost:8000/
```

`scripts/web-smoke.mjs` runs every example headlessly through Chrome/CDP.

## How a Run works

1. your `main.c` is compiled by clang.wasm in a Web Worker
2. ld.lld links the rootserver against the packaged seL4 libraries
   (`sysroot.tar.gz`, produced by `scripts/build-sysroot.py`)
3. the worker rebuilds the boot CPIO (kernel.elf + kernel.dtb + rootserver),
   relinks the elfloader and prepends the prebuilt OpenSBI firmware
4. the image boots on TinyEmu wasm in another worker; HTIF console output
   streams to the terminal

`scripts/build-app.sh` is the same pipeline runnable natively (great for
testing example code quickly).
