# dhad

`dhad` is an experimental compiler for a small Arabic C-like programming language.
It is still very much in its infancy: the grammar is evolving, the runtime is tiny,
and most subsystems are rough prototypes.

The language uses a handwritten LALR(1) parser and an LLVM backend (so hopefully a WASM interpreter soon).

## Getting Started

1. **Install prerequisites**

   - CMake 3.22 or newer
   - LLVM (version >= 17)
   - Clang/Clang++ toolchain (incl. `clang-format`)
   - Python 3.10 or newer

2. **Clone and configure**
   ```bash
   git clone https://example.com/dhad.git
   cd dhad
   cmake -S . -B build
   ```
3. **Build the compiler**
   ```bash
   make
   ```
4. **Run the smoke tests (lexer, parser, codegen)**
   ```bash
   make test
   ```
5. **Try the hello world example**
   ```bash
   ./build/dhad examples/hello-world.dh -o hello
   ./hello
   ```

## Project Notes

- The standard library is injected by default (so `اطبع` works without an import). Imports are for
  pulling in other source files: `استورد foo;` loads `foo.dh` from the same directory.
- Programs should expose an entry point called `دالة بداية()`.
- The parser/generator infrastructure relies on Python scripts in `tools/`.
  Regenerate tables with `cmake --build build --target parser_tables_gen` if you
  edit the grammar.
- `make format` runs `clang-format` over `src/` and `tests/`.

## Embedding (Playground)

`dhad` exposes a reusable pipeline API in `src/pipeline/compiler.hpp`:

- `compileString(...)` for parse + typecheck + IR generation.
- `runString(...)` for parse + typecheck + AST interpretation with buffered output.

For browser-style multi-file projects, inject modules from memory using `RunOptions::moduleResolver`:

```cpp
std::unordered_map<std::string, std::string> modules{
    {"util", u8R"(دالة حي(): نص { أعد "ok"؛ })"},
    {"util.dh", u8R"(دالة حي(): نص { أعد "ok"؛ })"},
};

dhad::pipeline::RunOptions opts;
opts.sourceName = "<memory>";
opts.moduleResolver = [&modules](std::string_view module, std::string_view) {
  auto it = modules.find(std::string(module));
  if (it == modules.end()) {
    return std::optional<dhad::pipeline::ResolvedModule>{};
  }
  return std::optional<dhad::pipeline::ResolvedModule>{
      dhad::pipeline::ResolvedModule{std::string(module), it->second}};
};

auto result = dhad::pipeline::runString(u8R"(استورد util؛ دالة بداية(): عدد { اطبع(حي())؛ أعد 0؛ })", opts);
```

## Browser MVP

1. Build the wasm web API with Emscripten:
   ```bash
   emcmake cmake -S . -B build/wasm -DDHAD_BUILD_WASM=ON
   EM_CACHE=/tmp/emscripten-cache cmake --build build/wasm -j
   ```
   This writes `dhad_web.js` and `dhad_web.wasm` into `build/`.
2. Serve this repository root over HTTP (worker + wasm require it):
   ```bash
   python3 -m http.server 8080
   ```
3. Open `http://localhost:8080/web/`.
4. Keep `WASM JS path` as `../build/dhad_web.js` (auto-default when served from `/web/`).
5. Click `Run` or `Parse AST`; the worker initializes automatically on first use.

### GitHub Pages deployment

This repository includes `.github/workflows/pages.yml` that:

1. Builds wasm artifacts with Emscripten.
2. Assembles a static `site/` folder from `web/*` plus:
   - `build/dhad_web.js` -> `site/build/dhad_web.js`
   - `build/dhad_web.wasm` -> `site/build/dhad_web.wasm`
3. Deploys to GitHub Pages via `actions/deploy-pages`.

For Pages-hosted site root, the playground auto-defaults `WASM JS path` to `./build/dhad_web.js`.

Contributions are welcome, just be aware that the design is changing quickly and I am still figuring out the fundamentals. Tune in via issues/PRs if you want to help shape the language.
