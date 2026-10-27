const MAX_OUTPUT_CHARS = 16000;
const RUN_TIMEOUT_MS = 2000;
const INIT_TIMEOUT_MS = 10000;

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

function resetWorker(reason = new Error("Worker was reset")) {
  for (const entry of state.pending.values()) {
    entry.reject(reason);
  }
  state.pending.clear();
  if (state.worker) {
    state.worker.terminate();
  }
  state.worker = null;
  state.initialized = false;
  state.initializing = null;
  state.currentModulePath = "";
  const worker = new Worker("./worker.js");
  state.worker = worker;
  worker.onmessage = (event) => {
    if (state.worker !== worker) return;
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
  const failWorker = (event) => {
    if (state.worker !== worker) return;
    if (event.preventDefault) event.preventDefault();
    resetWorker(new Error(event.message || "Worker communication failed"));
  };
  worker.onerror = failWorker;
  worker.onmessageerror = failWorker;
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
  state.initializing = callWorker({ type: "init", modulePath }, INIT_TIMEOUT_MS);
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
          resetWorker(new Error("Execution timed out"));
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
    try {
      state.worker.postMessage({ ...payload, id });
    } catch (error) {
      const entry = state.pending.get(id);
      state.pending.delete(id);
      entry.reject(error);
    }
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
      const res = await callWorker(
        { type: "parse", source: el.source.value }, RUN_TIMEOUT_MS,
      );
      setAst(res.ast);
      setOutput(res.stdout);
      setDiagnostics(res.stderr);
    } catch (error) {
      setDiagnostics(error.message);
      setAst("");
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
      setDiagnostics(res.stderr);
      setOutput(res.stdout);
      setAst(res.ast);
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
