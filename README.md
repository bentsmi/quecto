# Quecto Programming Language

Quecto is an in-development programming language. It features a full compiler pipeline:
tokenization, parsing, analysis, CFG IR emission, IR passes including SSA transformation, and code generation.

For more in-depth information, see [Overview](./OVERVIEW.md).

## Requirements

- A C11 compiler (`clang` or `gcc`; MSVC is untested) and `gperf`.
- Running included examples through `make run-*` requires `nasm` and a platform-specific linker.

## Getting Started

Build the compiler and run a few examples.

```bash
  $ make
  Built main
  $ make run-array
  AAAA
  AAAA
  AAAA
  AAAA
  AAAA
  19
  $ make run-proc
  148
```

The `run-*` rule accepts an example name, builds it, links it, runs it, and prints the exit code, which is typically the result of a calculation in the examples.

## Usage

The binary itself currently only has one command: `build`
which can be used as such:

```bash
  ./build/main build examples/array.q
```

Currently, it is hardcoded to produce `./out.S` but will be more configurable in the future.
Following the compilation run:

```bash
  nasm -f elf64 -o out.o out.S # Linux x86-64
  ld ... -o out out.o
```

To see what linker arguments should be used, refer to the `run-*` rule in the Makefile. Currently, the Makefile supports macOS and Linux.

## Smoke Tests

`make smoke` builds the examples in `examples/`. It verifies that expected-success examples compile and expected-failure examples fail.

## Current Status

Implemented:
  - tokenizer and recursive descent parser
  - basic analysis and type checking
  - CFG-based IR
  - dominance, phi insertion, liveness and register-coloring passes
  - NASM x86-64 backend
  - small test suite (small tokenizer test + smoke tests)

Known Limitations:
  - limited testing beyond tokenizer
  - limited compiler output options (`out.S` is currently hardcoded)
  - no register spilling yet (!)
  - backend is experimental
