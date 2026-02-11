# dhad ﺽ

`dhad` (ﺽ) is an experimental compiler (+ WASM interpreter) for a small Arabic programming language.
It is still very much in its infancy: the grammar is evolving, the runtime is tiny,
and most subsystems are rough prototypes.

The language uses a handwritten LALR(1) parser and an LLVM backend.

🧪 Try it out online on the [web playground](https://mosadhan.com/dhad)

> I probably should've written the README and docs in Arabic, but I don't have an arabic keyboard
> nor the energy atm.

## Getting Started

### Downloading Prebuilt Binary
This is probabily the most straightforward way to get things going

1. **Go to the repository [Releases](https://github.com/mqqz/dhad/releases) page**

2. **Download the archive for your platform**:
   - `dhad-linux-x86_64.tar.gz`
   - `dhad-macos-x86_64.tar.gz`
   - `dhad-macos-arm64.tar.gz`
   - `dhad-windows-x86_64.zip` (On Windows, the AST interpreter is shipped, not the compiler)

3. **Extract the archive**
   
   **Linux/macOS**:
   ```bash
   tar -xzf dhad-<platform>.tar.gz
   cd dhad-<platform>
   ```
   
   **Windows (PowerShell)**:
   ```powershell
   Expand-Archive .\dhad-windows-x86_64.zip -DestinationPath .
   cd .\dhad-windows-x86_64
   ```

5. **Run the compiler**

   e.g. run the `hello-world` example
   
   **Linux/macOS**:
   ```bash
   chmod +x dhad
   ./dhad examples/hello-world.dh -o hello
   ./hello
   ```
   
   **Windows (PowerShell)**:
   ```powershell
   .\dhad.exe examples\hello-world.dh -o hello.exe
   .\hello.exe
   ```

### Building From Source
Alternatively, if you'd rather build from source:

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
- `requirements.txt` has the requirements for the python scripts.
- Checkout `examples/` for a basic feel on how things work.
- `--emit-ir` flag can be used to output LLVM IR only (only Linux/MacOS).


*Contributions are welcome, just be aware that the design is changing quickly and I am still figuring out the fundamentals. 
Tune in via issues/PRs if you want to help shape the language.*
