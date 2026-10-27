let wasmModule = null;

function invokeSource(type, source) {
  const module = wasmModule;
  let sourcePtr = 0;
  let resultPtr = 0;
  try {
    sourcePtr = module.stringToNewUTF8(source);
    if (!sourcePtr) {
      throw new Error("Could not allocate source buffer");
    }
    resultPtr = type === "parse"
      ? module._dhad_parse_ast(sourcePtr)
      : module._dhad_run(sourcePtr);
    if (!resultPtr) {
      throw new Error("Could not allocate result buffer");
    }
    const result = JSON.parse(module.UTF8ToString(resultPtr));
    if (!result || typeof result.success !== "boolean" ||
        typeof result.stdout !== "string" || typeof result.stderr !== "string" ||
        typeof result.ast !== "string" || !Number.isInteger(result.exitCode)) {
      throw new Error("Invalid response from WASM module");
    }
    return {
      success: result.success,
      stdout: result.stdout,
      stderr: result.stderr,
      ast: result.ast,
      exitCode: result.exitCode,
    };
  } finally {
    if (resultPtr) module._dhad_free(resultPtr);
    if (sourcePtr) module._dhad_free(sourcePtr);
  }
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

    if (msg.type === "parse" || msg.type === "run") {
      const result = invokeSource(msg.type, String(msg.source || ""));
      self.postMessage({ ...result, id: msg.id, ok: true, type: msg.type });
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
