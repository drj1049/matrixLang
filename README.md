# MatrixLang

A small compiler for a domain-specific language built around matrix operations. Built as a Compiler Design course project. Supports declaring matrices (including negative numbers, `identity(n)` and `zeros(r, c)`), multiplying by a number, negating, adding, subtracting, multiplying, transposing, inverting, and printing them. Programs compile to plain C, either a standalone program or a function other C code can call, and chains of multiplications are reordered to the cheapest order.

## Example program

```
A = [[1,2],[3,4]]
B = [[5,6],[7,8]]
C = A + B
D = add(A, B, C)
E = multiply(A, B, C, D)
print(E)
```

## The language at a glance

```
A = [[1,-2.5],[-3,4]]        # a matrix literal; numbers may be negative
I = identity(2)              # 2x2 identity matrix
Z = zeros(2,3)               # 2x3 matrix of zeros
B = A * I + A                # infix + - *
S = 2 * A                    # multiply every element by a number (A * 2 works too)
N = -A                       # same as -1 * A
C = multiply(A, I, A)        # or add / subtract / multiply with 2 or more matrices
D = transpose(A)             # transpose / inverse take one matrix
print(B)
```

`identity` and `zeros` take whole numbers from 1 to 10000 (and at most 10 million elements in total). They are keywords, so they cannot be used as variable names. A negative number is written directly inside a matrix literal, as in `[[1,-2.5]]`. A number has to be multiplied with a matrix: `2 * A` and `A * 0.5` are fine, but `X = 5`, `A + 5` and `2 * 3` are errors. `-A` and `A - -B` work. Numbers and `-` apply to a single matrix, ID, literal or `identity`/`zeros`, not to a function call such as `add(...)` (there are no parentheses in expressions), so put the call in its own line first. Scaling by numbers next to each other is folded at compile time, so `2 * A * 3` becomes `6 * A`.

## Project structure

```
matrixLang/
  src/
    lexer.h    lexer.c      tokenizes source code
    ast.h      ast.c        AST node definitions and construction
    parser.h   parser.c     recursive descent parser
    semantic.h semantic.c   dimension checking and symbol table
    tac.h      tac.c        three-address code generation
    optimizer.h optimizer.c matrix chain multiplication reordering (dynamic programming)
    interp.h   interp.c     executes TAC, does the actual matrix math
    codegen.h  codegen.c    turns TAC into C code (a main(), or a function plus a header)
    matrt.h                 small C runtime used by the generated code (tiled multiply etc.)
  tests/
    *_test.c                one driver per stage, plus codegen_test.c (the compiler itself)
    func_caller.c           calls the generated functions the way a real project would
    sample*.matlang         valid and deliberately broken programs
  Makefile                  make, make test, make run FILE=...
  LICENSE                   MIT
  matlangc                  one command: compile a .matlang file to C, build it and run it
```

## Pipeline

Source code goes through these stages, each one buildable and runnable on its own:

1. **Lexer** — turns source text into tokens
2. **Parser** — turns tokens into an AST
3. **Semantic analysis** — checks matrix dimensions are compatible everywhere, annotates the AST with result dimensions
4. **TAC generation** — converts the checked AST into three-address code
5. **Optimizer** — reorders multiply chains so the fewest multiplications are done (runs after semantic analysis, before TAC)
6. **Execution** — runs the TAC in an interpreter and prints real computed matrices
7. **Code generation** — writes the TAC out as C, using a loop-tiled multiply for cache locality

## Quick start

```
make test                             # build everything and run all checks
./matlangc tests/sample1.matlang      # compile to C, build it, run it
./matlangc -O0 tests/sample4_chain.matlang   # same, with the optimizer switched off
```

## Turning the optimizer off

By default the multiply-chain optimizer runs (`-O1`). Pass `-O0` to skip it, so multiplications are compiled in exactly the order they are written. That is handy for showing the before and after of the optimization:

```
./matlangc -O0 tests/sample9_chain_func.matlang
./build/compiler -O0 tests/sample4_chain.matlang out.c
make run FILE=tests/sample4_chain.matlang OPT=-O0
```

`tac_test` and `interp_test` take the same flag. In the web page the Optimizer box in the top bar does the same thing.

## Building and running

Each stage has its own test driver so you can run the pipeline incrementally. Compile from the project root.

**Lexer**
```
gcc -Wall -o tests/lexer_test tests/lexer_test.c src/lexer.c -Isrc
./tests/lexer_test tests/sample1.matlang
```

**Parser**
```
gcc -Wall -o tests/parser_test tests/parser_test.c src/lexer.c src/ast.c src/parser.c -Isrc
./tests/parser_test tests/sample1.matlang
```

**Semantic analysis**
```
gcc -Wall -o tests/semantic_test tests/semantic_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c -Isrc
./tests/semantic_test tests/sample1.matlang
```

**TAC generation**
```
gcc -Wall -o tests/tac_test tests/tac_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c src/tac.c -Isrc
./tests/tac_test tests/sample1.matlang
```

**Full execution**
```
gcc -Wall -o tests/interp_test tests/interp_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c src/tac.c src/interp.c -Isrc -lm
./tests/interp_test tests/sample1.matlang
```

Note the `-lm` flag on the last one, it links the math library, required since the execution engine uses `fabs()` for matrix inversion.

To see error handling in action, run any of the above against `tests/sample2_bad.matlang` instead. It contains a dimension mismatch and should exit with a semantic error and line number instead of crashing.

## Web page

A small local page for trying programs without the terminal. It shows the program output, the generated C, the three-address code, and the optimizer's tree before and after.

```
python3 server.py        # or: make ui
```

Then open http://localhost:8000. Pick a sample or type your own program and press Run (Cmd+Enter). The server only listens on your own computer, and needs Python 3, gcc and make. Programs with `input`/`output` show the generated function and header instead of running.

## Compiling to a callable C function

A program that declares its matrices with `input` and names a result with `output` compiles to a C function instead of a standalone program:

```
input A 2x3
input B 3x2
C = A * B
output C
```

```
./matlangc tests/sample5_func.matlang              # function is called matlang_compute
./matlangc tests/sample5_func.matlang my_function  # or pick the name
```

This writes `build/<name>.c` and a header `build/<name>.h` declaring it, e.g. `Mat matlang_compute(Mat A, Mat B);`. Add the `.c` to your project, compile with `-Isrc` (for `matrt.h`) and call it. The header also works from C++ projects:

```c
#include "sample5_func.h"

double a[] = {1, 2, 3, 4, 5, 6};   /* row-major */
Mat A = {2, 3, a};
...
Mat C = matlang_compute(A, B);
/* use C.d[i * C.c + j] */
mat_free(C);
```

Rules:

- Inputs are only read. The function never modifies or frees them, and a program cannot assign to an input.
- The result is always newly allocated (even for `output A`), and the caller frees it with `mat_free`. Everything else the function allocates is freed before it returns.
- Sizes are fixed at compile time, which is what lets the optimizer reorder multiply chains. Passing a matrix of a different size prints an error and exits. So does `inverse` of a singular matrix.
- One `output` per program, as the last statement.
- Names of the form `t0`, `t1`, ... are reserved for the compiler's temporaries.
- These programs cannot be run by the interpreter (`interp_test`), since the numbers only exist at call time. Programs with typed-in matrices and no `input`/`output` work exactly as before.

## Status

Working: lexer, parser, AST, semantic analysis with symbol table, negative numbers in matrix literals, `identity` and `zeros`, scalar multiplication and unary minus, an `-O0` switch for the optimizer, matrix chain multiplication optimizer, three-address code generation, an interpreter that performs real matrix arithmetic including inversion via Gauss-Jordan elimination, and C code generation (standalone program or callable function) with a loop-tiled multiply.

`make test` checks that compiled output matches the interpreter (with the optimizer on and off), that the generated functions return correct, freshly allocated results, and that the broken samples are rejected.
