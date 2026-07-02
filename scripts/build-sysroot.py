#!/usr/bin/env python3
"""Package the in-browser build sysroot.

Collects everything a browser-side clang/lld needs to rebuild the rootserver
and reassemble the boot image -- headers, static libraries, crt/elfloader
objects, linker scripts, kernel.elf/kernel.dtb, the OpenSBI firmware prefix --
and bakes the exact compile/link recipes (extracted from build/build.ninja,
same approach as scripts/build-app.sh) into a manifest.json with placeholder
arguments. Output: build/web/sysroot.tar.gz with manifest.json as the first
entry.

Sysroot layout:
  build/...   files from the CMake build tree (generated headers, .a, .o)
  src/...     source-tree headers (projects/, kernel/, tools/)
  gcc/...     files taken from the cross-gcc installation (crtbegin, libgcc)
"""

import io
import json
import os
import shlex
import subprocess
import sys
import tarfile

CLANG = "/opt/homebrew/opt/llvm/bin/clang"
LLD = "/opt/homebrew/opt/lld/bin/ld.lld"

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(REPO, "build")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BUILD, "web", "sysroot.tar.gz")

OPENSBI_PREFIX_SIZE = 0x200000

PLACEHOLDERS = {"{main_c}", "{main_o}", "{rootserver}", "{archive_o}", "{elfloader}"}


def ninja_command(target):
    out = subprocess.run(
        ["ninja", "-t", "commands", target],
        cwd=BUILD, check=True, capture_output=True, text=True,
    ).stdout.strip().splitlines()[-1]
    if out.startswith(": && "):
        out = out[len(": && "):]
    if out.endswith(" && :"):
        out = out[: -len(" && :")]
    return shlex.split(out)


def gcc_file(*args):
    return subprocess.run(
        ["riscv64-elf-gcc", "-march=rv64imafdc_zicsr_zifencei", "-mabi=lp64d", *args],
        check=True, capture_output=True, text=True,
    ).stdout.strip()


LIBGCC = gcc_file("-print-libgcc-file-name")
GCC_ROOT = os.path.dirname(LIBGCC)

MAPS = [
    (BUILD + "/", "build/"),
    (os.path.join(REPO, "projects") + "/", "src/projects/"),
    (os.path.join(REPO, "kernel") + "/", "src/kernel/"),
    (os.path.join(REPO, "tools") + "/", "src/tools/"),
    (GCC_ROOT + "/", "gcc/"),
]

files = {}        # sysroot path -> host path
include_dirs = {} # sysroot path -> host path


def map_path(host):
    host = os.path.abspath(host)
    for prefix, repl in MAPS:
        if host.startswith(prefix):
            return repl + host[len(prefix):]
    sys.exit(f"error: path outside mapped trees: {host}")


def resolve(token):
    """Return absolute host path if token names an existing file, else None."""
    for cand in (token, os.path.join(BUILD, token)):
        if os.path.isabs(cand) or cand is not token:
            if os.path.isfile(cand):
                return os.path.abspath(cand)
    return None


def add_file(host):
    sp = map_path(host)
    files[sp] = host
    return sp


def add_include(host):
    sp = map_path(host)
    include_dirs[sp] = host
    return sp


def rewrite(tokens, replace=()):
    """Rewrite a gcc command's tokens into a portable clang argv."""
    replace = dict(replace)
    out = []
    it = iter(tokens)
    for tok in it:
        if tok in replace:
            out.append(replace[tok])
            continue
        if tok.startswith("--sysroot=") or tok in ("-MD",):
            continue
        if tok in ("-MT", "-MF"):
            next(it)
            continue
        if tok == "-isystem":
            out += [tok, add_include(next(it))]
            continue
        if tok.startswith("-I"):
            out.append("-I" + add_include(tok[2:]))
            continue
        if tok == "-lgcc":
            out.append(add_file(LIBGCC))
            continue
        host = resolve(tok)
        if host:
            out.append(add_file(host))
            continue
        out.append(tok)
    return out


# --- compile recipe -------------------------------------------------------
compile_tokens = ninja_command("projects/hello/CMakeFiles/hello.dir/src/main.c.obj")
src = next(t for t in compile_tokens if t.endswith("/main.c"))
obj = compile_tokens[compile_tokens.index("-o") + 1]
compile_args = rewrite(compile_tokens[1:], {src: "{main_c}", obj: "{main_o}"})

# --- link recipes ----------------------------------------------------------
# The browser runs ld.lld directly (a wasm clang cannot spawn a linker
# subprocess), so ask the clang driver (-###) what linker command line the
# gcc-style link would expand to, and bake that raw lld argv instead.
def linker_argv(driver_args):
    out = subprocess.run(
        [CLANG, "--target=riscv64-unknown-elf", f"--ld-path={LLD}", "-###",
         *[a for a in driver_args if not a.startswith("--sysroot=")]],
        cwd=BUILD, check=True, capture_output=True, text=True,
    ).stderr.splitlines()[-1]
    argv = [t.strip('"') for t in shlex.split(out)]
    assert argv[0] == LLD, f"unexpected linker: {argv[0]}"
    return argv[1:]


link_tokens = ninja_command("projects/hello/hello")
link_args = rewrite(
    linker_argv(link_tokens[1:]),
    {
        "projects/hello/CMakeFiles/hello.dir/src/main.c.obj": "{main_o}",
        "projects/hello/hello": "{rootserver}",
    },
)
link_args.insert(link_args.index("-o"), "--strip-debug")

elfloader_tokens = ninja_command("elfloader-tool/elfloader")
elfloader_args = rewrite(
    linker_argv(elfloader_tokens[1:]),
    {
        "elfloader-tool/archive.o": "{archive_o}",
        "elfloader-tool/elfloader": "{elfloader}",
    },
)

# --- app-independent payload pieces ---------------------------------------
cpio = [
    {"name": "kernel.elf", "path": add_file(os.path.join(BUILD, "elfloader-tool/kernel.elf"))},
    {"name": "kernel.dtb", "path": add_file(os.path.join(BUILD, "kernel/kernel.dtb"))},
]
with open(os.path.join(BUILD, "images/hello-image-riscv-spike.bin"), "rb") as f:
    opensbi_prefix = f.read(OPENSBI_PREFIX_SIZE)

manifest = {
    "target": "riscv64-unknown-elf",
    "compile": compile_args,
    "rootserver_link": link_args,
    "elfloader_link": elfloader_args,
    "archive_asm": [
        "-march=rv64imafdc_zicsr_zifencei", "-mabi=lp64d",
        "-c", "{archive_s}", "-o", "{archive_o}",
    ],
    "cpio": cpio,
    "opensbi_prefix": "opensbi-prefix.bin",
}

# --- write the tarball -----------------------------------------------------
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with tarfile.open(OUT, "w:gz") as tar:

    def add_bytes(name, data):
        info = tarfile.TarInfo(name)
        info.size = len(data)
        tar.addfile(info, io.BytesIO(data))

    add_bytes("manifest.json", json.dumps(manifest, indent=1).encode())
    add_bytes("opensbi-prefix.bin", opensbi_prefix)

    n_headers = 0
    for sp, host in sorted(include_dirs.items()):
        for root, _, names in os.walk(host):
            for name in names:
                if name.endswith((".h", ".inc", ".def")):
                    hp = os.path.join(root, name)
                    tar.add(hp, sp + hp[len(host):])
                    n_headers += 1
    for sp, host in sorted(files.items()):
        tar.add(host, sp)

print(f"{OUT}: {os.path.getsize(OUT)} bytes "
      f"({len(files)} files, {n_headers} headers from {len(include_dirs)} include dirs)")
