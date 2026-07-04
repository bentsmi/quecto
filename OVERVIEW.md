# General Overview

## Goals

Quecto is meant to be an experimental systems language, but right now the compiler pipeline is being developed before
higher-level language constructs are added. The language design is still exploratory, so syntax and semantics may change as the compiler matures.

Currently the desired grammar looks like this:
```
  proc divide (dividend: i32, divisor: i32) => (quotient: i32, remainder: i32) {
    return (dividend / divisor, dividend % divisor);
  }

  proc main () => (status: i32) {
    i := 0;
    while i < 30 {
      q, r := divide(i, 15);
      if (q % 3 == 0)
        print("bow");
      
      if (r == 2 or r == 9)
        print("wow");
      else
        print("now");
    }
    
    return 0;  
  }
```
Currently implemented, either fully or partially:
  - external procedures, procedures and calls
  - basic integer types
  - declarations and assignments
  - if/elif/else, while
  - arrays, braced initializers (partially)
  - pointers/references/indexing (no dereferencing yet)

Analysis is currently limited in its ability to reject incorrect programs before later compiler stages.
Many operations are also not implemented yet, such as `!=`.

## Compiler

For the source code:
  - all sizes/counts use or should use `size_t`
  - sets use `size_t` to refer to the bit index
  - all memory should be allocated at the beginning (not true for hashtables and codegen... yet)

Currently the Quecto Compiler has 6 stages:
  - Lexer
  - Parsing
  - Analysis
  - IR Generation
  - CFG + IR Analysis
  - Code Generation

### Lexing

Nothing out of the ordinary, it is a handwritten tokenizer/lexer.
`src/keywords.c` is generated from `src/keywords.gperf` by the `gperf` utility, which creates a perfect
hash table for use in the tokenizer. One goal is to write our own `gperf`, though it is not a priority.

Related Files:
  - `include/tokenizer.h`
  - `src/tokenizer.c`
  - `src/keywords.c` generated from `src/keywords.gperf`
  - `src/keywords.gperf`
  
### Parsing

Generation of an AST is done via recursive descent parsing.

Related Files:
  - `include/parser.h`
  - `src/parser.c`
  - `include/ast.h`
  - `src/ast.c`

### Analysis

Parsing only builds the AST structure. Analysis assigns meaning to symbols and checks whether the program is semantically valid.

Related Files:
  - `include/symbol_table.h`
  - `src/symbol_table.c`
  - `include/analysis.h`
  - `src/analysis.c`

### IR Emission

Quecto uses its own intermediate representation contained within a control-flow graph. This
stage depends on analysis because emitted IR assumes symbols and types have already been resolved.

Related Files:
  - `include/ir.h`
  - `include/cfg.h`
  - `include/emission.h`
  - `src/ir.c`
  - `src/emission.c`
  - `src/cfg.c`

### CFG + IR Analysis

After emission, there are a series of passes done on the Quecto IR to transform it into SSA so that more advanced optimizations can be implemented in the future.
For now, register allocation is done using a "ColorPool" that allows for register hints.

Passes, in execution order:
  - `passes_preparation` - runs CFG analysis to fill data needed for passes
  - `pass_insert_phis` - uses dominance frontier to place phis
  - `pass_rename` - replaces any slots that are promoted to vregs
  - `pass_kill_slots` - marks unused slots as dead
  - `pass_remove_copies` - removes unnecessary copies and rewrites uses of deleted vregs
  - `pass_liveness` - computes vreg sets of `live_in` and `live_out` for each block
  - `pass_compute_liveness` - depends on `pass_liveness`; computes the liveness interval of each vreg
  - `pass_crosses_call` - depends on `pass_liveness`; marks vregs that cross a call
  - `pass_precoloring` - precolors parameters, arguments, and return values (planned to do more, e.g. divides)
  - `pass_color_cfg` - uses precolor hints and a pool to allocate physical registers for vregs
    - requires `pass_liveness`, `pass_compute_liveness`, `pass_insert_phis`, and `pass_rename`
  - `pass_phis_into_copies` - lowers phis into copies
  - `pass_sweep_nops` - allocates new instruction lists for blocks, deleting all NOPs (nulled instructions)

Debug Passes:
  - `debug_pass_print` - prints CFG
  - `debug_pass_print_colored` - prints CFG with colors instead of vregs

Related Files:
  - `include/passes.h`
  - `include/cfg.h`
  - `src/passes.c`
  - `src/cfg.c`

### Code Generation

Currently, Quecto is hardcoded to output NASM assembly with helpers to link the assembly for either macOS or Linux in the `Makefile`.
There are future plans for a slew of backends. Most of these backends translate the IR to the desired assembly convention,
though there are some adherence rules based on that assembly's conventions.

The only implemented backend right now is Linux x86-64, though macOS x86-64 is supported through compile-time flags.

Related Files:
  - `include/codegen.h`
  - `include/backends/linux_x64.h`
  - `include/backends/linux_arm64.h`
  - `include/backends/mac_arm64.h`
  - `include/backends/windows_x64.h`
  - `src/codegen.c`
  - `src/backends/linux_x64.c`
  - `src/backends/linux_arm64.c`
  - `src/backends/mac_arm64.c`
  - `src/backends/windows_x64.c`
