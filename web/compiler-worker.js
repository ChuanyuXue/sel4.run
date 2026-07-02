/*
 * In-browser build pipeline: compiles the user's main.c with a wasm build
 * of clang, links the rootserver and relinks the elfloader with wasm ld.lld,
 * and assembles the final boot image. Mirrors scripts/build-app.sh; the
 * recipes come baked into sysroot.tar.gz (see scripts/build-sysroot.py).
 *
 * in:  {type:"build", source:string}
 * out: {type:"ready"}
 *      {type:"log", data:string}
 *      {type:"built", image:ArrayBuffer}   (transferred)
 *      {type:"error", message:string}
 */
"use strict";

const enc = new TextEncoder();
const dec = new TextDecoder();

/* ---- minimal tar reader (ustar/pax as written by python tarfile) ---- */
function untar(u8) {
    const files = new Map();
    let off = 0;
    while (off + 512 <= u8.length) {
        const name = dec.decode(u8.subarray(off, off + 100)).replace(/\0.*$/s, "");
        if (!name) break;
        const size = parseInt(
            dec.decode(u8.subarray(off + 124, off + 136)).replace(/\0.*$/s, "").trim(), 8) || 0;
        const type = u8[off + 156];
        const prefix = dec.decode(u8.subarray(off + 345, off + 500)).replace(/\0.*$/s, "");
        if (type === 0x30 || type === 0) {  /* regular file */
            files.set(prefix ? prefix + "/" + name : name,
                      u8.slice(off + 512, off + 512 + size));
        }
        off += 512 + Math.ceil(size / 512) * 512;
    }
    return files;
}

/* ---- cpio newc writer (same layout as scripts/build-app.sh) ---- */
function cpioNewc(entries) {
    const parts = [];
    const emit = (name, data, mode, nlink) => {
        const fields = [1, mode, 0, 0, nlink, 0, data.length, 0, 0, 0, 0,
                        name.length + 1, 0];
        const hdr = "070701" + fields.map(
            (f) => f.toString(16).toUpperCase().padStart(8, "0")).join("");
        const h = enc.encode(hdr + name + "\0");
        parts.push(h, new Uint8Array((4 - (h.length % 4)) % 4));
        parts.push(data, new Uint8Array((4 - (data.length % 4)) % 4));
    };
    for (const [name, data] of entries) emit(name, data, 0o100644, 1);
    emit("TRAILER!!!", new Uint8Array(0), 0, 1);
    let len = 0;
    for (const p of parts) len += p.length;
    const out = new Uint8Array(len);
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
}

/* ---- toolchain ---- */
let sysroot = null;   /* Map path -> Uint8Array */
let manifest = null;
const factories = {};
const wasmModules = {};   /* tool -> compiled WebAssembly.Module */

/* the tool wasm is shipped in slices (Cloudflare Pages caps files at
 * 25 MiB); reassemble and compile once, then reuse the module per Run */
async function loadToolWasm(name, nparts) {
    const parts = await Promise.all(
        Array.from({ length: nparts }, (_, i) =>
            fetch(name + ".part" + i).then((r) => {
                if (!r.ok) throw new Error(name + ".part" + i + ": HTTP " + r.status);
                return r.arrayBuffer();
            })));
    let len = 0;
    for (const p of parts) len += p.byteLength;
    const bytes = new Uint8Array(len);
    let off = 0;
    for (const p of parts) { bytes.set(new Uint8Array(p), off); off += p.byteLength; }
    return WebAssembly.compile(bytes);
}

async function init() {
    const resp = await fetch("sysroot.tar.gz");
    if (!resp.ok) throw new Error("sysroot.tar.gz: HTTP " + resp.status);
    const raw = await new Response(
        resp.body.pipeThrough(new DecompressionStream("gzip"))).arrayBuffer();
    sysroot = untar(new Uint8Array(raw));
    manifest = JSON.parse(dec.decode(sysroot.get("manifest.json")));
    const toolchain = await (await fetch("toolchain.json")).json();
    for (const tool of ["clang", "lld", "llvm-objcopy"]) {
        importScripts(tool + ".js");
        factories[tool] = self.LLVMTool;
        wasmModules[tool] = await loadToolWasm(tool + ".wasm",
                                               toolchain[tool + ".wasm"] || 1);
    }
    postMessage({ type: "ready" });
}

async function runTool(tool, argv, extraFiles, outputs) {
    let log = "";
    const mod = await factories[tool]({
        print: (s) => { log += s + "\n"; },
        printErr: (s) => { log += s + "\n"; },
        noInitialRun: true,
        /* lld is a generic driver and picks its personality from argv[0] */
        thisProgram: "/bin/" + (tool === "lld" ? "ld.lld" : tool),
        /* instantiate from the pre-compiled module instead of re-fetching
         * and re-compiling the wasm on every run */
        instantiateWasm: (info, receive) => {
            WebAssembly.instantiate(wasmModules[tool], info).then(receive);
            return {};
        },
    });
    const seen = new Set();
    const write = (path, data) => {
        const parts = path.split("/");
        let d = "";
        for (let i = 0; i < parts.length - 1; i++) {
            d += "/" + parts[i];
            if (!seen.has(d)) {
                try { mod.FS.mkdir(d); } catch (e) { /* exists */ }
                seen.add(d);
            }
        }
        mod.FS.writeFile("/" + path, data);
    };
    for (const [p, data] of sysroot) write(p, data);
    for (const [p, data] of Object.entries(extraFiles)) write(p, data);
    let code = 1;
    try {
        code = mod.callMain(argv);
    } catch (e) {
        if (e && e.name === "ExitStatus") code = e.status;
        else throw e;
    }
    const files = {};
    if (code === 0)
        for (const p of outputs) files[p] = mod.FS.readFile("/" + p);
    return { code, log, files };
}

/* rewrite manifest args: placeholders + sysroot-relative paths */
function fix(args, subs) {
    return args.map((a) => {
        if (a in subs) return subs[a];
        if (/^(build|src|gcc)\//.test(a)) return "/" + a;
        if (a.startsWith("-I") && /^(build|src|gcc)\//.test(a.slice(2)))
            return "-I/" + a.slice(2);
        return a;
    });
}

const SUBS = {
    "{main_c}": "/work/main.c",
    "{main_o}": "/work/main.o",
    "{rootserver}": "/work/rootserver",
    "{archive_s}": "/work/archive.s",
    "{archive_o}": "/work/archive.o",
    "{elfloader}": "/work/elfloader",
};

async function build(source) {
    const say = (s) => postMessage({ type: "log", data: s });
    const cc = ["--target=" + manifest.target];
    const step = async (label, tool, argv, extra, outputs) => {
        say(label);
        const r = await runTool(tool, argv, extra, outputs);
        if (r.log) say(r.log.trimEnd());
        if (r.code !== 0) throw new Error(label + " failed (exit " + r.code + ")");
        return r.files;
    };

    const o1 = await step("compiling main.c", "clang",
        [...cc, ...fix(manifest.compile, SUBS)],
        { "work/main.c": enc.encode(source) }, ["work/main.o"]);

    const o2 = await step("linking rootserver", "lld",
        fix(manifest.rootserver_link, SUBS),
        { "work/main.o": o1["work/main.o"] }, ["work/rootserver"]);

    const cpio = cpioNewc([
        ...manifest.cpio.map((e) => [e.name, sysroot.get(e.path)]),
        ["rootserver", o2["work/rootserver"]],
    ]);
    const archiveS =
        '.section ._archive_cpio,"aw"\n' +
        ".globl _archive_start, _archive_start_end\n" +
        "_archive_start:\n" +
        '.incbin "/work/archive.cpio"\n' +
        "_archive_start_end:\n";
    const o3 = await step("packing boot archive", "clang",
        [...cc, ...fix(manifest.archive_asm, SUBS)],
        { "work/archive.cpio": cpio, "work/archive.s": enc.encode(archiveS) },
        ["work/archive.o"]);

    const o4 = await step("linking elfloader", "lld",
        fix(manifest.elfloader_link, SUBS),
        { "work/archive.o": o3["work/archive.o"] }, ["work/elfloader"]);

    const o5 = await step("flattening image", "llvm-objcopy",
        ["-O", "binary", "/work/elfloader", "/work/payload.bin"],
        { "work/elfloader": o4["work/elfloader"] }, ["work/payload.bin"]);

    const prefix = sysroot.get(manifest.opensbi_prefix);
    const payload = o5["work/payload.bin"];
    const image = new Uint8Array(prefix.length + payload.length);
    image.set(prefix, 0);
    image.set(payload, prefix.length);
    return image.buffer;
}

const ready = init().catch((e) =>
    postMessage({ type: "error", message: "toolchain init: " + e.message }));

onmessage = async (e) => {
    if (e.data.type !== "build") return;
    try {
        await ready;
        const image = await build(e.data.source);
        postMessage({ type: "built", image }, [image]);
    } catch (err) {
        postMessage({ type: "error", message: String(err.message || err) });
    }
};
