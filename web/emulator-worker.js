/*
 * Runs TinyEmu in a dedicated worker: the main thread stays responsive and
 * Stop/Reset can hard-kill the machine with worker.terminate().
 *
 * in:  {type:"start", image:ArrayBuffer, ramMb, termSize:[cols,rows]}
 *      {type:"key", data:string}
 * out: {type:"out", data:string}   console output (HTIF + emulator stdout)
 *      {type:"started"}
 *      {type:"exit"}               machine powered off
 */
"use strict";

let termSize = [100, 32];
let emu = null;

/* console interface expected by TinyEmu's js/lib.js */
self.term = {
    write(s) { postMessage({ type: "out", data: s }); },
    getSize() { return termSize; },
};

onmessage = async (e) => {
    const m = e.data;
    if (m.type === "start") {
        termSize = m.termSize || termSize;
        importScripts("riscvemu64-wasm.js");
        const bios = URL.createObjectURL(new Blob([m.image]));
        const cfg = URL.createObjectURL(new Blob([JSON.stringify({
            version: 1,
            machine: "riscv64",
            memory_size: m.ramMb,
            bios: bios,
        })]));
        emu = await TinyEmu({
            print: (s) => postMessage({ type: "out", data: s + "\r\n" }),
            printErr: (s) => postMessage({ type: "out", data: s + "\r\n" }),
            onExit: () => postMessage({ type: "exit" }),
        });
        emu.ccall("vm_start", null,
            ["string", "number", "string", "number",
             "number", "number", "number"],
            [cfg, m.ramMb, "", 0, 0, 0, 0]);
        postMessage({ type: "started" });
    } else if (m.type === "key" && emu) {
        for (let i = 0; i < m.data.length; i++)
            emu.ccall("console_queue_char", null, ["number"],
                      [m.data.charCodeAt(i)]);
    }
};

/* TinyEmu's HTIF poweroff path calls exit(); surface it as a clean event
 * instead of an unhandled worker error. */
self.onerror = () => { postMessage({ type: "exit" }); return true; };
self.onunhandledrejection = (e) => {
    e.preventDefault();
    postMessage({ type: "exit" });
};
