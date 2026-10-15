# MatrixLang

![CI](https://github.com/drj1049/matrixLang/actions/workflows/ci.yml/badge.svg)

**Try it online: https://matrixlang.onrender.com** (on the free plan the site sleeps when nobody is using it, so the first visit can take about a minute to wake up).

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
G = (A + B) * A              # parentheses group, so a sum can sit inside a product
S = 2 * A                    # multiply every element by a number (A * 2 works too)
N = -A                       # same as -1 * A
C = multiply(A, I, A)        # or add / subtract / multiply with 2 or more matrices
D = transpose(A)             # transpose / inverse take one matrix
print(B)
```

`identity` and `zeros` take whole numbers from 1 to 10000 (and at most 10 million elements in total). They are keywords, so they cannot be used as variable names. A negative number is written directly inside a matrix literal, as in `[[1,-2.5]]`. A number has to be multiplied with a matrix: `2 * A` and `A * 0.5` are fine, but `X = 5`, `A + 5` and `2 * 3` are errors. `-A` and `A - -B` work. Parentheses group an expression, so `(A + B) * C`, `2 * (A - B)` and `-(A + B)` all work, and nest up to 200 levels. What is inside the parentheses must be a matrix expression, so `(2) * A` is an error (write `2 * A`). Numbers and `-` apply to a matrix, ID, literal, `identity`/`zeros` or a parenthesized expression, not to a function call such as `add(...)`, so put the call in its own line first. A statement can hold at most 2000 operators, one multiplication chain at most 63 matrices, and one `add`, `subtract` or `multiply` call at most 16 matrices. Scaling by numbers next to each other is folded at compile time, so `2 * A * 3` becomes `6 * A`.

## Project structure

```
matrixLang/
  src/
    lexer.h    lexer.c      tokenizes source code
    ast.h      ast.c        AST node definitions and construction
    diag.h     diag.c       prints errors with the source line and a ^ under the problem
    parser.h   parser.c     recursive descent parser
    semantic.h semantic.c   dimension checking and symbol table
    tac.h      tac.c        three-address code generation
    optimizer.h optimizer.c matrix chain multiplication reordering (dynamic programming)
    tacopt.h   tacopt.c     TAC optimizer: common subexpression reuse, algebraic rewrites, dead code elimination
    interp.h   interp.c     executes TAC, does the actual matrix math
    codegen.h  codegen.c    turns TAC into C code (a main(), or a function plus a header)
    matrt.h                 small C runtime used by the generated code (tiled multiply etc.)
  tests/
    *_test.c                one driver per stage, plus codegen_test.c (the compiler itself)
    func_caller.c           calls the generated functions the way a real project would
    fuzz.py                 random program tester (checks the compiler against Python)
    phase3_tests.py         12 test cases, expected answers worked out with numpy (needs numpy)
    sample*.matlang         valid and deliberately broken programs
  .github/workflows/ci.yml  runs the checks on every push
  Dockerfile  render.yaml   put the web page online (see "Putting it online")
  server.py   runlimited.py the web page's server, and the helper that limits what a run may use
  Makefile                  make, make test, make run FILE=...
  LICENSE                   MIT
  matlangc                  one command: compile a .matlang file to C, build it and run it
  bench_chain.sh            benchmark: one program built with -O0 and -O1, run time and memory
```

## Pipeline

Source code goes through these stages, each one buildable and runnable on its own:

1. **Lexer** — turns source text into tokens
2. **Parser** — turns tokens into an AST
3. **Semantic analysis** — checks matrix dimensions are compatible everywhere, annotates the AST with result dimensions
4. **TAC generation** — converts the checked AST into three-address code
5. **Optimizer** — reorders multiply chains so the fewest multiplications are done (runs after semantic analysis, before TAC)
6. **TAC optimizer** — reuses repeated expressions and applies algebraic rewrites (runs on the TAC)
7. **Dead code elimination**: removes instructions whose result is never used (runs on the TAC, after the TAC optimizer)
8. **Execution** — runs the TAC in an interpreter and prints real computed matrices
9. **Code generation** — writes the TAC out as C, using a loop-tiled multiply for cache locality

## Grammar

The parser is recursive descent with one token of lookahead (LL(1)). Every rule below is one function in `src/parser.c`.

```
program      -> statement* EOF
statement    -> ID '=' ( funcCall | expr )
              | 'print' '(' ID ')'
              | 'input' ID DIMS
              | 'output' ID

funcCall     -> ( 'add' | 'subtract' | 'multiply' ) '(' ID ( ',' ID )+ ')'
              | ( 'transpose' | 'inverse' ) '(' ID ')'

expr         -> term ( ( '+' | '-' ) term )*
term         -> unary ( '*' unary )*
unary        -> '-' unary | NUMBER | factor
factor       -> ID | matrixLiteral | matrixGen | '(' expr ')'

matrixGen    -> 'identity' '(' NUMBER ')'
              | 'zeros' '(' NUMBER ',' NUMBER ')'
matrixLiteral-> '[' row ( ',' row )* ']'
row          -> '[' value ( ',' value )* ']'
value        -> [ '-' ] NUMBER

NUMBER       -> digit+ [ '.' digit+ ]
DIMS         -> digit+ 'x' digit+          (for example 2x3)
ID           -> ( letter | '_' ) ( letter | digit | '_' )*
comment      -> '#' up to the end of the line
```

**Precedence and associativity** (tightest first):

| Level | Operator | Associativity |
|-------|----------|---------------|
| 0 | `( ... )` | overrides everything below |
| 1 | unary `-` | right (`- - A` is `-(-A)`) |
| 2 | `*` | left |
| 3 | `+` `-` | left |

So `A - B - C` is `(A - B) - C`, `2 * A + B * C` is `(2 * A) + (B * C)`, and `A - (B - C)` keeps its own grouping. Parentheses only change how the tree is built, there is no node for them. For a multiplication chain the optimizer ignores the grouping the user wrote and picks the cheapest one anyway, so `A * (B * C)` is rearranged to `(A * B) * C` when that is cheaper (`-O0` keeps the written order). The function calls (`add(...)`, `transpose(...)` and so on) are only allowed as the whole right-hand side of an assignment.

Two rules are checked after parsing because the grammar alone cannot express them: a product may not end up as a bare number (`2 * 3` and `A + 5` are errors), and `ID`s named like keywords or `t0`, `t1`, ... are reserved.

## Error recovery

The compiler reports up to 20 errors in one run instead of stopping at the first. After a syntax error it skips ahead (panic mode) to the next `print`, `input`, `output` or a name at the start of a later line, and carries on parsing from there. The semantic checker does the same per statement. A variable whose definition failed is marked as bad, so later uses of it do not produce a pile of follow-up errors. The run ends with `N errors found.`; the web page marks every offending line in the gutter. Each error shows the source line with a caret under the problem, like this:

```
Parse error at line 4, column 15: expected ']' to close matrix literal (got 'print' on line 5)
 4 | C = A * [[1,2]
   |               ^
```

When something is missing at the end of a line, the caret sits just after the last token of that line, not on the next line.

## TAC optimizer

After the TAC is generated, `tacopt.c` runs local value numbering over it: every value gets a number describing how it was computed, and a computation that was already done is replaced by a copy of the earlier result. Turn it off with `-O0`.

- **Common subexpressions**: if `A * B` (or `A + B`, in either order) is computed twice, the second one reuses the first.
- **Algebraic rewrites**: `A * identity(n)` and `identity(n) * A` become `A`; `A + zeros(...)` becomes `A`; `A * zeros(...)` becomes `zeros(...)` of the right size; `1 * A` becomes `A`; `0 * A` becomes `zeros(...)`; `transpose(transpose(A))` becomes `A`; `inverse(inverse(A))` becomes `A`; `transpose` or `inverse` of `identity` is `identity`.

The `inverse(inverse(A))` rewrite is exact in maths, but with floating-point numbers `-O0` (which really inverts twice) can differ from the optimized result in the last digits.

## Dead code elimination

Once the TAC optimizer is done, `tac_eliminate_dead()` (also in `tacopt.c`) removes every instruction whose result is never read. It goes through the TAC from the last instruction to the first and keeps a set of the names that are still needed, starting from every `print` and `output`. An assignment to a name that is not in that set is dead and is dropped without marking its operands as needed, so a whole chain of unused work disappears in one pass. The code has no loops or branches, so one backward pass is exact.

```
C = A * B          # removed: C is only used by D
D = C + A          # removed: D is never used
print(A)
```

It also cleans up after the other optimizations. In `H = A * identity(2)` the product becomes a plain copy of `A`, which leaves the `identity(2)` unused, and that is removed too. `./build/tac_test` lists every removal as a `REMOVE` line after the code.

Three kinds of instruction are never removed:

- `print` and `output`, since they are what the program is for.
- `input`. Every input is part of the generated function's signature and has its size checked, even when the program never reads it.
- `inverse(...)`. Inverting a singular matrix stops the program with an error, so dropping an unused `inverse` would hide that error and the program would behave differently from the same program with `-O0`. Its argument is kept as well.

`-O0` switches it off together with the other TAC optimizations. `tests/sample25_dead.matlang` is an example (the expected TAC and notes are in `tests/sample25_dead.tac.expected`), `tests/sample26_dead_func.matlang` is a function with dead code and an unused input, and `tests/sample27_all_dead.matlang` is a program where everything is removed.

## Quick start

```
make test                             # build everything and run all checks
make fuzz                             # test the compiler on random programs (make fuzz N=500 SEED=3)
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
gcc -Wall -o tests/parser_test tests/parser_test.c src/lexer.c src/ast.c src/diag.c src/parser.c -Isrc
./tests/parser_test tests/sample1.matlang
```

**Semantic analysis**
```
gcc -Wall -o tests/semantic_test tests/semantic_test.c src/lexer.c src/ast.c src/diag.c src/parser.c src/semantic.c -Isrc
./tests/semantic_test tests/sample1.matlang
```

**TAC generation**
```
gcc -Wall -o tests/tac_test tests/tac_test.c src/lexer.c src/ast.c src/diag.c src/parser.c src/semantic.c src/optimizer.c src/tac.c src/tacopt.c -Isrc
./tests/tac_test tests/sample1.matlang
```

**Full execution**
```
gcc -Wall -o tests/interp_test tests/interp_test.c src/lexer.c src/ast.c src/diag.c src/parser.c src/semantic.c src/optimizer.c src/tac.c src/tacopt.c src/interp.c -Isrc -lm
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

## Putting it online

The page is not static: for every Run the server compiles the program to C and builds it with gcc. So it needs a host that can run a container with Python, gcc and make (GitHub Pages and Netlify cannot). The `Dockerfile` sets that up, and `render.yaml` describes the service for Render.

On Render (free plan): create an account, choose New > Web Service, connect this GitHub repository, and leave the settings alone. Render finds the Dockerfile, builds it, and gives you a link like `https://matrixlang.onrender.com`. Choosing New > Blueprint instead reads `render.yaml`. After that every push to `main` redeploys the site by itself.

Because strangers can use the link, the server runs in public mode (`MATRIXLANG_PUBLIC=1`, set in the Dockerfile) and limits every run:

- the compiled program gets 5 seconds and 256 MB of memory, and may print at most 1 MB (the page shows the first 200,000 characters)
- the compiler and gcc get 10 seconds and 768 MB
- two runs at a time (others wait up to 20 seconds), and 20 runs a minute per visitor
- programs are limited to 20,000 characters, and run as an ordinary user inside the container

The free plan puts the site to sleep after 15 minutes without visitors, and the first visit afterwards takes about a minute to wake it up. `/healthz` answers `ok` and is what the host uses to check the server is alive.

To try the same setup on your own computer (needs Docker): `docker build -t matrixlang .` then `docker run -p 8000:10000 -e PORT=10000 matrixlang`, and open http://localhost:8000.

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
- Every `input` is a parameter of the function, in the order written, even if the program never uses it. Its size is still checked.
- The result is always newly allocated (even for `output A`), and the caller frees it with `mat_free`. Everything else the function allocates is freed before it returns.
- Sizes are fixed at compile time, which is what lets the optimizer reorder multiply chains. Passing a matrix of a different size prints an error and exits. So does `inverse` of a singular matrix.
- One `output` per program, as the last statement.
- Names of the form `t0`, `t1`, ... are reserved for the compiler's temporaries.
- These programs cannot be run by the interpreter (`interp_test`), since the numbers only exist at call time. Programs with typed-in matrices and no `input`/`output` work exactly as before.

## Status

Working: lexer, parser, AST, a written grammar, parentheses in expressions, reporting several errors at once with a caret under each problem, semantic analysis with symbol table, negative numbers in matrix literals, `identity` and `zeros`, scalar multiplication and unary minus, an `-O0` switch for the optimizer, matrix chain multiplication optimizer, common subexpression reuse, algebraic rewrites and dead code elimination on the TAC, three-address code generation, an interpreter that performs real matrix arithmetic including inversion via Gauss-Jordan elimination, and C code generation (standalone program or callable function) with a loop-tiled multiply.

`make servercheck` starts the web server in the same public mode it uses online and checks its limits: it stops a program that runs too long, needs too much memory or prints too much, refuses requests from other websites, and limits how often one visitor can run programs.

`make sanitize` builds the compiler with the address and undefined-behaviour sanitizers and runs every sample through it. The same commands (`make test`, `make fuzz`, `make sanitize` and `make servercheck`) run on GitHub for every push (see the CI badge at the top).

`make test` checks that compiled output matches the interpreter (with the optimizer on and off), that the generated functions return correct, freshly allocated results, that dead code is removed (while an unused `inverse` is kept, so a singular matrix is still reported), and that the broken samples are rejected with exactly the expected messages and carets (the expected text is kept in `tests/*.expected`). `make fuzz` generates random programs and checks every stage (interpreter and compiled C, optimizer on and off) against an independent Python calculation. Some of the programs print only some of their variables, which puts dead code through the same check.

`tests/phase3_tests.py` is a separate script with 12 test cases (two each for valid, invalid, semantic, boundary, run-time and complex programs). It works out the expected answers with numpy and plain Python instead of reading them from the compiler, so it needs numpy (`pip3 install numpy`). Run `make` first, then `python3 tests/phase3_tests.py`. It ends with a line such as `tests: 12 run, 12 passed, 0 failed`. Nothing else in the project needs numpy.

`bench_chain.sh` shows what the matrix chain optimizer is worth. It builds a three matrix product (1000x20, 20x1000 and 1000x20), compiles it with `-O0` and `-O1`, checks that both builds print the same result, and then prints the median run time of nine runs and the peak memory of each. Run `make` first, then `bash bench_chain.sh`. Neither script is part of the CI checks.
