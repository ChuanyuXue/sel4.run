# sel4.run

seL4 running on TinyEmu (RISC-V64), natively and in the browser.

## Checkout

```sh
repo init -u git@github.com:ChuanyuXue/sel4.run.git -m manifest.xml
repo sync
```

## Build and run (native)

```sh
sel4.run/scripts/build.sh
sel4.run/scripts/run.sh
```

## Build and run (browser)

Needs emscripten (macOS: `brew install emscripten`) and a native build first:

```sh
sel4.run/scripts/build-web.sh
sel4.run/scripts/run-web.sh     # then open http://localhost:8000/
```

Both targets boot the same `build/images/hello-image-riscv-spike.bin`
(OpenSBI firmware wrapping the ElfLoader, seL4 kernel and rootserver).
