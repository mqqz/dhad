#!/usr/bin/env node
// Exercise worker ownership and UI message handling without requiring WASM.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const web = path.resolve(__dirname, "../web");

async function testWorker() {
  let next = 1;
  let allocated = 0;
  let freed = 0;
  let mode = "success";
  const buffers = new Map();
  const messages = [];
  const alloc = (value) => {
    allocated++;
    const ptr = next++;
    buffers.set(ptr, value);
    return ptr;
  };
  const response = () => {
    if (mode === "trap") throw new Error("WASM trap");
    if (mode === "null-result") return 0;
    if (mode === "malformed-json") return alloc("{");
    if (mode === "invalid-envelope") return alloc("{}");
    return alloc(JSON.stringify({
      success: mode === "success",
      stdout: "error: literal program output\n",
      stderr: mode === "success" ? "" : "<memory>:1: error: overflow",
      ast: "Program",
      exitCode: mode === "success" ? 0 : 1,
    }));
  };
  const module = {
    stringToNewUTF8: (source) => mode === "null-source" ? 0 : alloc(source),
    _dhad_run: response,
    _dhad_parse_ast: response,
    UTF8ToString: (ptr) => buffers.get(ptr),
    _dhad_free: (ptr) => {
      assert(buffers.has(ptr), "invalid or double free");
      buffers.delete(ptr);
      freed++;
    },
  };
  const context = {
    self: {
      location: { href: "https://example.test/worker.js" },
      postMessage: (message) => messages.push(message),
    },
    URL,
    importScripts: () => { context.self.Module = async () => module; },
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(web, "worker.js"), "utf8"), context);
  let id = 0;
  const send = async (type, source = "نص") => {
    await context.self.onmessage({ data: { type, source, id: ++id } });
    const message = messages.at(-1);
    assert.equal(message.id, id);
    assert.equal(buffers.size, 0, "request leaked a WASM allocation");
    return message;
  };
  assert.equal((await send("run")).ok, false, "uninitialized calls must fail");
  await context.self.onmessage({ data: { type: "init", id: 0, modulePath: "./build/dhad_web.js" } });
  assert.equal(messages.at(-1).ok, true);
  for (let i = 0; i < 100; i++) {
    const result = await send(i % 2 ? "run" : "parse", "نص".repeat(10000));
    assert.equal(result.ok, true);
    assert.equal(result.success, true);
    assert.equal(result.stdout, "error: literal program output\n");
  }
  mode = "language-error";
  const languageError = await send("run");
  assert.equal(languageError.ok, true, "language failure is a valid transport response");
  assert.equal(languageError.success, false);
  assert.equal(languageError.stdout, "error: literal program output\n");
  assert.match(languageError.stderr, /overflow/);
  for (mode of ["malformed-json", "invalid-envelope", "null-source", "null-result", "trap"]) {
    assert.equal((await send("run")).ok, false, mode);
  }
  assert.equal((await send("unknown")).ok, false);
  assert.equal(allocated, freed, "all input and output allocations must be freed");
}

async function testUI() {
  const elements = new Map();
  for (const id of ["wasmPath", "exampleSelect", "parseBtn", "runBtn", "source", "output", "diagnostics", "ast"]) {
    elements.set(id, {
      value: "", textContent: "", disabled: false, handlers: {},
      addEventListener(type, callback) { this.handlers[type] = callback; },
      appendChild() {},
    });
  }
  const timers = new Map();
  let timerId = 0;
  let result = { success: true, stdout: "error: literal output", stderr: "", ast: "", exitCode: 0 };
  class Worker {
    terminate() { this.terminated = true; }
    postMessage(message) {
      queueMicrotask(() => {
        if (!this.terminated) this.onmessage({ data: { ...result, id: message.id, type: message.type, ok: true } });
      });
    }
  }
  const context = {
    Worker,
    window: { location: { pathname: "/dhad/" } },
    document: { getElementById: (id) => elements.get(id), createElement: () => ({}) },
    setTimeout: (callback) => { timers.set(++timerId, callback); return timerId; },
    clearTimeout: (id) => timers.delete(id),
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(web, "app.js"), "utf8"), context);
  const click = (id) => elements.get(id).handlers.click();
  await click("runBtn");
  assert.equal(elements.get("output").textContent, "error: literal output");
  assert.equal(elements.get("diagnostics").textContent, "");
  result = { success: false, stdout: "partial output", stderr: "<memory>:1: error: overflow", ast: "", exitCode: 1 };
  await click("runBtn");
  assert.equal(elements.get("output").textContent, result.stdout);
  assert.equal(elements.get("diagnostics").textContent, result.stderr);
  result = { success: false, stdout: "", stderr: "parse error", ast: "", exitCode: 1 };
  await click("parseBtn");
  assert.equal(elements.get("diagnostics").textContent, result.stderr);

  vm.runInContext("state.worker.postMessage = () => {}", context);
  const pending = vm.runInContext("callWorker({type: 'run'}, 2)", context);
  const rejection = assert.rejects(pending, /Execution timed out/);
  assert.equal(timers.size, 1);
  [...timers.values()][0]();
  await rejection;
  assert.equal(vm.runInContext("state.pending.size", context), 0);
  assert.equal(vm.runInContext("state.initialized", context), false);
  assert.equal(timers.size, 0);
  result = { success: true, stdout: "recovered", stderr: "", ast: "", exitCode: 0 };
  await click("runBtn");
  assert.equal(elements.get("output").textContent, "recovered");

  vm.runInContext("state.worker.postMessage = () => {}", context);
  const crashed = vm.runInContext("callWorker({type: 'parse'}, 2)", context);
  const crashRejection = assert.rejects(crashed, /worker crashed/);
  vm.runInContext("state.worker.onerror({message: 'worker crashed', preventDefault() {}})", context);
  await crashRejection;
  assert.equal(vm.runInContext("state.pending.size", context), 0);
  assert.equal(timers.size, 0);
}

(async () => {
  await testWorker();
  await testUI();
  process.stdout.write("Worker ownership, structured results, UI diagnostics, and recovery passed.\n");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
