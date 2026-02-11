const MAX_OUTPUT_CHARS = 16000;
const RUN_TIMEOUT_MS = 2000;

const EXAMPLES = {
  hello: `استورد أساس؛

دالة بداية(): عدد {
  اطبع("مرحبا من المتصفح")؛
  أعد 0؛
}
`,
  loops: `دالة بداية(): عدد {
  دع س: عدد = 5؛
  طالما (س > 0) {
    اطبع("tick")؛
    س = س - 1؛
  }
  أعد 0؛
}
`,
  arrays: `دالة بداية(): عدد {
  دع أ: [عدد] = [10، 20، 30]؛
  أ[1] = 99؛
  اطبع("done")؛
  أعد أ[1]؛
}
`,
};

const state = {
  worker: null,
  initialized: false,
  initializing: null,
  currentModulePath: "",
  reqId: 0,
  pending: new Map(),
};

const el = {
  wasmPath: document.getElementById("wasmPath"),
  exampleSelect: document.getElementById("exampleSelect"),
  parseBtn: document.getElementById("parseBtn"),
  runBtn: document.getElementById("runBtn"),
  source: document.getElementById("source"),
  output: document.getElementById("output"),
  diagnostics: document.getElementById("diagnostics"),
  ast: document.getElementById("ast"),
};

function defaultModulePath() {
  const path = window.location.pathname || "";
  if (path === "/web" || path.startsWith("/web/")) {
    return "../build/dhad_web.js";
  }
  return "./build/dhad_web.js";
}

function setDiagnostics(text) {
  el.diagnostics.textContent = text || "";
}

function setOutput(text) {
  let out = text || "";
  if (out.length > MAX_OUTPUT_CHARS) {
    out = out.slice(0, MAX_OUTPUT_CHARS) + "\n... output truncated ...";
  }
  el.output.textContent = out;
}

function setAst(text) {
  el.ast.textContent = text || "";
}

function resetWorker() {
  if (state.worker) {
    state.worker.terminate();
  }
  state.worker = new Worker("./worker.js");
  state.initialized = false;
  state.initializing = null;
  state.currentModulePath = "";
  state.worker.onmessage = (event) => {
    const msg = event.data || {};
    const entry = state.pending.get(msg.id);
    if (!entry) {
      return;
    }
    state.pending.delete(msg.id);
    if (msg.ok) {
      entry.resolve(msg);
    } else {
      entry.reject(new Error(msg.error || "Worker error"));
    }
  };
}

async function ensureWorkerReady() {
  const modulePath = (el.wasmPath.value || "").trim();
  if (!modulePath) {
    throw new Error("WASM JS path is required");
  }
  if (!state.worker) {
    resetWorker();
  }
  if (state.initialized && state.currentModulePath === modulePath) {
    return;
  }
  if (state.initializing && state.currentModulePath === modulePath) {
    await state.initializing;
    return;
  }

  if (state.currentModulePath !== modulePath) {
    resetWorker();
  }
  state.currentModulePath = modulePath;
  state.initializing = callWorker({ type: "init", modulePath });
  try {
    await state.initializing;
    state.initialized = true;
  } finally {
    state.initializing = null;
  }
}

function callWorker(payload, timeoutMs = 0) {
  if (!state.worker) {
    throw new Error("Worker not initialized");
  }
  const id = ++state.reqId;
  return new Promise((resolve, reject) => {
    const timer = timeoutMs
      ? setTimeout(() => {
          state.pending.delete(id);
          resetWorker();
          reject(new Error("Execution timed out"));
        }, timeoutMs)
      : null;

    state.pending.set(id, {
      resolve: (value) => {
        if (timer) clearTimeout(timer);
        resolve(value);
      },
      reject: (err) => {
        if (timer) clearTimeout(timer);
        reject(err);
      },
    });
    state.worker.postMessage({ ...payload, id });
  });
}

function bindExamples() {
  Object.keys(EXAMPLES).forEach((name) => {
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    el.exampleSelect.appendChild(opt);
  });
  el.exampleSelect.value = "hello";
  el.source.value = EXAMPLES.hello;
  if (!el.wasmPath.value.trim()) {
    el.wasmPath.value = defaultModulePath();
  }
}

function setBusy(isBusy) {
  el.parseBtn.disabled = isBusy;
  el.runBtn.disabled = isBusy;
}

async function init() {
  bindExamples();
  resetWorker();

  el.exampleSelect.addEventListener("change", () => {
    const key = el.exampleSelect.value;
    el.source.value = EXAMPLES[key] || "";
    setDiagnostics("");
    setAst("");
    setOutput("");
  });

  el.parseBtn.addEventListener("click", async () => {
    setBusy(true);
    try {
      await ensureWorkerReady();
      const res = await callWorker({ type: "parse", source: el.source.value });
      setAst(res.ast);
      if (res.ast.startsWith("error:")) {
        setDiagnostics(res.ast);
      } else {
        setDiagnostics("");
      }
    } catch (error) {
      setDiagnostics(error.message);
    } finally {
      setBusy(false);
    }
  });

  el.runBtn.addEventListener("click", async () => {
    setBusy(true);
    try {
      await ensureWorkerReady();
      const res = await callWorker(
        { type: "run", source: el.source.value },
        RUN_TIMEOUT_MS,
      );
      if (res.output.startsWith("error:")) {
        setDiagnostics(res.output);
        setOutput("");
      } else {
        setDiagnostics("");
        setOutput(res.output);
      }
    } catch (error) {
      setDiagnostics(error.message);
      setOutput("");
    } finally {
      setBusy(false);
    }
  });
}

init().catch((error) => {
  setDiagnostics(error.message);
});
