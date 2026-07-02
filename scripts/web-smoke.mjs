#!/usr/bin/env node
/*
 * Headless end-to-end smoke test for the lab: serves build/web, drives
 * Chrome over CDP, loads each example, clicks Run (compiling in-browser)
 * and asserts the expected terminal output appears.
 *
 * Usage: web-smoke.mjs [example-number ...]   (default: all)
 */

import { spawn } from "node:child_process";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const dist = path.join(repoRoot, "build", "web");
const PORT = 8793;
const CDP_PORT = 9224;
const CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";

/* what each example must print (see web/examples/) */
const EXPECT = {
    1: "hello from seL4 on TinyEmu!",
    2: "total non-device untyped",
    3: "seL4_CapInitThreadTCB",
    4: "after delete, kernel tag",
    5: "three round trips",
    6: "the word was cleared by the first wait",
    7: "worker checked in",
    8: "the memory is reusable again",
    9: "read back 0xc0ffee",
    10: "seL4_Fault_VMFault",
};
const wanted = process.argv.slice(2).map(Number);
const cases = Object.entries(EXPECT)
    .filter(([n]) => wanted.length === 0 || wanted.includes(Number(n)));

const serve = spawn("python3", ["-m", "http.server", "-d", dist, String(PORT)],
                    { stdio: "ignore" });
const chrome = spawn(CHROME,
    ["--headless", "--disable-gpu", "--no-first-run",
     `--user-data-dir=/tmp/sel4run-smoke-profile`,
     `--remote-debugging-port=${CDP_PORT}`,
     `http://127.0.0.1:${PORT}/`],
    { stdio: "ignore" });
const cleanup = (code) => { serve.kill(); chrome.kill(); process.exit(code); };
process.on("SIGINT", () => cleanup(1));

const getJson = (url) => new Promise((res, rej) =>
    http.get(url, (r) => {
        let d = "";
        r.on("data", (c) => (d += c));
        r.on("end", () => res(JSON.parse(d)));
    }).on("error", rej));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function connect() {
    for (let i = 0; i < 30; i++) {
        try {
            const targets = await getJson(`http://127.0.0.1:${CDP_PORT}/json`);
            const page = targets.find((t) => t.type === "page");
            if (page) return new WebSocket(page.webSocketDebuggerUrl);
        } catch (e) { /* not up yet */ }
        await sleep(500);
    }
    throw new Error("cannot connect to Chrome");
}

const ws = await connect();
await new Promise((r) => (ws.onopen = r));
let msgId = 0;
const pending = new Map();
ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) {
        pending.get(m.id)(m.result);
        pending.delete(m.id);
    }
};
const send = (method, params = {}) => new Promise((res) => {
    const id = ++msgId;
    pending.set(id, res);
    ws.send(JSON.stringify({ id, method, params }));
});
const evaljs = async (expression) =>
    (await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true }))
        ?.result?.value;

await send("Runtime.enable");

/* wait for the toolchain */
for (let i = 0; ; i++) {
    const s = await evaljs("window.__lab && __lab.status()");
    if (s === "toolchain ready") break;
    if (i > 120) { console.error("FAIL: toolchain never became ready"); cleanup(1); }
    await sleep(1000);
}
console.log("toolchain ready");

let failed = 0;
for (const [n, expect] of cases) {
    const file = await evaljs(
        `fetch("examples/index.json").then(r => r.json()).then(l => l[${n} - 1].file)`);
    await evaljs(`fetch(${JSON.stringify(file)}).then(r => r.text()).then(t => __lab.setCode(t))`);
    await evaljs("__lab.run()");
    let ok = false;
    for (let i = 0; i < 120 && !ok; i++) {
        await sleep(1000);
        ok = await evaljs(`__lab.output().includes(${JSON.stringify(expect)})`);
        const status = await evaljs("__lab.status()");
        if (status === "build failed") break;
    }
    console.log(`example ${n}: ${ok ? "PASS" : "FAIL"}`);
    if (!ok) {
        failed++;
        console.log(await evaljs("__lab.output().slice(-2000)"));
    }
}
cleanup(failed ? 1 : 0);
