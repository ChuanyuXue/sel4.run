/* sel4.run lab: editor + in-browser build + TinyEmu, glued together. */

import { EditorView, basicSetup } from "https://esm.sh/codemirror@6.0.1";
import { cpp } from "https://esm.sh/@codemirror/lang-cpp@6.0.2";
import { oneDark } from "https://esm.sh/@codemirror/theme-one-dark@6.1.2";

const $ = (id) => document.getElementById(id);
const RAM_MB = 256;

/* ---- terminal ---- */
const term = new Terminal({
    convertEol: true,
    fontFamily: "Menlo, Consolas, monospace",
    fontSize: 13,
    scrollback: 10000,
    theme: { background: "#16161e" },
});
const fit = new FitAddon.FitAddon();
term.loadAddon(fit);
term.open($("terminal"));
fit.fit();
addEventListener("resize", () => fit.fit());

let outputLog = "";
const emit = (s) => {
    outputLog += s;
    term.write(s);
};
const dim = (s) => {
    for (const line of s.split("\n"))
        emit("\x1b[90m" + line + "\x1b[0m\r\n");
};

/* ---- editor ---- */
const view = new EditorView({
    parent: $("editor"),
    extensions: [basicSetup, cpp(), oneDark],
});
const getCode = () => view.state.doc.toString();
const setCode = (text) =>
    view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: text } });

/* ---- status ---- */
const setStatus = (s) => {
    $("status").textContent = s;
    document.title = "sel4.run - " + s;
};

/* ---- compiler worker ---- */
let compilerReady = false;
const compiler = new Worker("compiler-worker.js");
compiler.onmessage = (e) => {
    const m = e.data;
    if (m.type === "ready") {
        compilerReady = true;
        setStatus("toolchain ready");
        $("run").disabled = false;
    } else if (m.type === "log") {
        dim(m.data);
    } else if (m.type === "built") {
        lastImage = m.image;
        startEmulator(m.image);
    } else if (m.type === "error") {
        dim(m.message);
        setStatus("build failed");
        $("run").disabled = !compilerReady;
    }
};

/* ---- emulator worker ---- */
let emu = null;
let lastImage = null;

function stopEmulator() {
    if (emu) {
        emu.terminate();
        emu = null;
    }
}

function startEmulator(image) {
    stopEmulator();
    setStatus("booting");
    emu = new Worker("emulator-worker.js");
    emu.onmessage = (e) => {
        const m = e.data;
        if (m.type === "out") emit(m.data);
        else if (m.type === "started") setStatus("running");
        else if (m.type === "exit") setStatus("machine halted");
    };
    /* the image buffer is transferred, so hand the worker a copy */
    emu.postMessage(
        { type: "start", image: image.slice(0), ramMb: RAM_MB,
          termSize: [term.cols, term.rows] },
    );
    $("run").disabled = !compilerReady;
    $("stop").disabled = false;
    $("reset").disabled = false;
}

term.onData((data) => emu && emu.postMessage({ type: "key", data }));

/* ---- toolbar ---- */
$("run").onclick = () => {
    stopEmulator();
    term.clear();
    setStatus("compiling");
    $("run").disabled = true;
    compiler.postMessage({ type: "build", source: getCode() });
};
$("stop").onclick = () => {
    stopEmulator();
    setStatus("stopped");
    $("stop").disabled = true;
};
$("reset").onclick = () => lastImage && startEmulator(lastImage);

/* ---- examples (start on the first one) ---- */
const select = $("example");
const loadExample = async (file) => setCode(await (await fetch(file)).text());
fetch("examples/index.json")
    .then((r) => r.json())
    .then((examples) => {
        for (const ex of examples) {
            const opt = document.createElement("option");
            opt.value = ex.file;
            opt.textContent = ex.title;
            select.appendChild(opt);
        }
        loadExample(examples[0].file);
    });
select.onchange = () => loadExample(select.value);

setStatus("loading toolchain…");

/* hook for headless (CDP) smoke tests */
window.__lab = {
    term, getCode, setCode, startEmulator,
    run: () => $("run").click(),
    output: () => outputLog,
    status: () => $("status").textContent,
};
