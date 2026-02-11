let wasmModule = null;

function readCString(module, ptr) {
  if (!ptr) {
    return "";
  }
  const text = module.UTF8ToString(ptr);
  module._dhad_free(ptr);
  return text;
}

async function initModule(modulePath) {
  // Classic worker path: load generated Emscripten JS then create module.
  importScripts(modulePath);
  const factory = self.Module;
  if (typeof factory !== "function") {
    throw new Error("Emscripten module factory not found at path: " + modulePath);
  }
  const moduleUrl = new URL(modulePath, self.location.href);
  const baseUrl = new URL(".", moduleUrl).href;
  wasmModule = await factory({
    locateFile: (fileName) => new URL(fileName, baseUrl).href,
  });
}

self.onmessage = async (event) => {
  const msg = event.data || {};
  try {
    if (msg.type === "init") {
      await initModule(msg.modulePath);
      self.postMessage({ id: msg.id, ok: true, type: "init" });
      return;
    }

    if (!wasmModule) {
      throw new Error("WASM module is not initialized. Click Init Worker first.");
    }

    if (msg.type === "parse") {
      const ptr = wasmModule._dhad_parse_ast(wasmModule.allocateUTF8(msg.source || ""));
      const ast = readCString(wasmModule, ptr);
      self.postMessage({ id: msg.id, ok: true, type: "parse", ast });
      return;
    }

    if (msg.type === "run") {
      const ptr = wasmModule._dhad_run(wasmModule.allocateUTF8(msg.source || ""));
      const output = readCString(wasmModule, ptr);
      self.postMessage({ id: msg.id, ok: true, type: "run", output });
      return;
    }

    throw new Error("Unknown worker message type: " + String(msg.type));
  } catch (error) {
    self.postMessage({
      id: msg.id,
      ok: false,
      type: msg.type,
      error: error instanceof Error ? error.message : String(error),
    });
  }
};
