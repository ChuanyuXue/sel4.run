#!/bin/sh
#
# Rebuild ONLY the rootserver from a user main.c and assemble the final
# TinyEmu boot image, without going through CMake -- using clang + ld.lld.
#
# This mirrors, step by step, what the browser build does: the exact
# compile/link recipes are extracted from build/build.ninja (fully expanded
# via `ninja -t commands`) and re-targeted from the GNU cross toolchain to
# clang/lld. Everything except the user's main.c comes prebuilt from a
# regular scripts/build.sh run:
#
#   1. clang -c main.c                        -> main.o
#   2. ld.lld rootserver link (18 .a + crt)   -> rootserver (debug stripped)
#   3. cpio(kernel.elf, kernel.dtb, rootserver) wrapped via .incbin
#   4. ld.lld elfloader relink                -> elfloader ELF
#   5. llvm-objcopy -O binary                 -> payload
#   6. opensbi prefix [0, 0x200000) + payload -> final .bin
#
# Usage: build-app.sh <main.c> [output.bin]
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")
build="$repo_root/build"

main_c=${1:?usage: build-app.sh <main.c> [output.bin]}
main_c=$(CDPATH='' cd -- "$(dirname -- "$main_c")" && pwd -P)/$(basename "$main_c")
out_bin=${2:-$build/app-build/app-image.bin}
case "$out_bin" in
    /*) ;;
    *) out_bin="$PWD/$out_bin" ;;
esac

llvm_bin=/opt/homebrew/opt/llvm/bin
lld=/opt/homebrew/opt/lld/bin/ld.lld
clang="$llvm_bin/clang"
objcopy="$llvm_bin/llvm-objcopy"
target="--target=riscv64-unknown-elf"

# GNU cpio (macOS BSD cpio lacks --reproducible/--append)
if command -v brew > /dev/null 2>&1; then
    cpio_prefix=$(brew --prefix cpio 2> /dev/null || true)
    if [ -n "$cpio_prefix" ] && [ -d "$cpio_prefix/bin" ]; then
        PATH="$cpio_prefix/bin:$PATH"
    fi
fi

if [ ! -f "$build/build.ninja" ]; then
    echo "error: $build not configured; run scripts/build.sh first" >&2
    exit 1
fi

libgcc=$(riscv64-elf-gcc -march=rv64imafdc_zicsr_zifencei -mabi=lp64d \
    -print-libgcc-file-name)
libgcc_dir=$(dirname "$libgcc")

work="$build/app-build"
mkdir -p "$work"
cd "$build"

# Pull the fully-expanded reference commands out of the ninja build graph.
compile_cmd=$(ninja -t commands projects/hello/CMakeFiles/hello.dir/src/main.c.obj | tail -1)
link_cmd=$(ninja -t commands projects/hello/hello | tail -1)
elfloader_cmd=$(ninja -t commands elfloader-tool/elfloader | tail -1)

# Re-target a gcc driver command line to clang + ld.lld.
retarget() {
    sed -e "s|^: && ||" -e "s| && :||" \
        -e "s|/opt/homebrew/bin/riscv64-elf-gcc|$clang $target --ld-path=$lld|" \
        -e "s|-lgcc|-L$libgcc_dir -lgcc|"
}

# 1. compile user main.c
step1=$(printf '%s' "$compile_cmd" | retarget |
    sed -e "s|-o projects/hello/[^ ]*\.obj|-o $work/main.o|" \
        -e "s|-c /Users[^ ]*/main\.c|-c $main_c|" \
        -e "s|-MF [^ ]*||" -e "s|-MT [^ ]*||" -e "s|-MD||")
eval "$step1"

# 2. link rootserver (strip debug info in the same step, replacing
#    the separate riscv64-elf-strip the CMake build does)
step2=$(printf '%s' "$link_cmd" | retarget |
    sed -e "s|projects/hello/CMakeFiles/hello.dir/src/main.c.obj|$work/main.o|" \
        -e "s|-o projects/hello/hello|-Wl,--strip-debug -o $work/rootserver|")
eval "$step2"

# 3. cpio archive: kernel.elf, kernel.dtb, rootserver (reproducible newc,
#    same entry names/order as the CMake build)
cp elfloader-tool/kernel.elf kernel/kernel.dtb "$work/"
rm -f "$work/archive.cpio"
(
    cd "$work"
    : | cpio --reproducible --quiet --create -H newc --file=archive.cpio
    for f in kernel.elf kernel.dtb rootserver; do
        touch -d 1970-01-01T00:00:00Z "$f"
        echo "$f" | cpio --append --reproducible --owner=+0:+0 --quiet \
            -o -H newc --file=archive.cpio
    done
    printf '.section ._archive_cpio,"aw"\n.globl _archive_start, _archive_start_end\n_archive_start:\n.incbin "%s/archive.cpio"\n_archive_start_end:\n' "$work" > archive.o.S
)
"$clang" $target -march=rv64imafdc_zicsr_zifencei -mabi=lp64d \
    -c -o "$work/archive.o" "$work/archive.o.S"

# 4. relink elfloader with the new archive
step4=$(printf '%s' "$elfloader_cmd" | retarget |
    sed -e "s|elfloader-tool/archive.o|$work/archive.o|" \
        -e "s|-o elfloader-tool/elfloader|-o $work/elfloader|")
eval "$step4"

# 5. flatten + 6. prepend the (payload-independent) OpenSBI firmware
"$objcopy" -O binary "$work/elfloader" "$work/payload.bin"
head -c 2097152 opensbi/platform/generic/firmware/fw_payload.bin > "$out_bin"
cat "$work/payload.bin" >> "$out_bin"

echo "Image: $out_bin ($(wc -c < "$out_bin" | tr -d ' ') bytes)"
