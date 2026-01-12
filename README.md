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

Contributions are welcome, just be aware that the design is changing quickly and I am still figuring out the fundamentals. Tune in via issues/PRs if you want to help shape the language.
