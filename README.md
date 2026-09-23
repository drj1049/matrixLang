# MatrixLang

A small compiler for a domain-specific language built around matrix operations. Built as a Compiler Design course project. Supports declaring matrices, adding, subtracting, multiplying, transposing, inverting, and printing them.

## Example program

```
A = [[1,2],[3,4]]
B = [[5,6],[7,8]]
C = A + B
D = add(A, B, C)
E = multiply(A, B, C, D)
print(E)
```

## Project structure

```
matrixLang/
  src/
    lexer.h    lexer.c      tokenizes source code
    ast.h      ast.c        AST node definitions and construction
    parser.h   parser.c     recursive descent parser
    semantic.h semantic.c   dimension checking and symbol table
    tac.h      tac.c        three-address code generation
    interp.h   interp.c     executes TAC, does the actual matrix math
  tests/
    lexer_test.c
    parser_test.c
    semantic_test.c
    tac_test.c
    interp_test.c
  sample1.matlang       valid program
  sample2_bad.matlang   invalid program, used to test error handling
```

## Pipeline

Source code goes through five stages, each one buildable and runnable on its own:

1. **Lexer** — turns source text into tokens
2. **Parser** — turns tokens into an AST
3. **Semantic analysis** — checks matrix dimensions are compatible everywhere, annotates the AST with result dimensions
4. **TAC generation** — converts the checked AST into three-address code
5. **Execution** — runs the TAC and prints real computed matrices

## Building and running

Each stage has its own test driver so you can run the pipeline incrementally. Compile from the project root.

**Lexer**
```
gcc -Wall -o tests/lexer_test tests/lexer_test.c src/lexer.c -Isrc
./tests/lexer_test sample1.matlang
```

**Parser**
```
gcc -Wall -o tests/parser_test tests/parser_test.c src/lexer.c src/ast.c src/parser.c -Isrc
./tests/parser_test sample1.matlang
```

**Semantic analysis**
```
gcc -Wall -o tests/semantic_test tests/semantic_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c -Isrc
./tests/semantic_test sample1.matlang
```

**TAC generation**
```
gcc -Wall -o tests/tac_test tests/tac_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c src/tac.c -Isrc
./tests/tac_test sample1.matlang
```

**Full execution**
```
gcc -Wall -o tests/interp_test tests/interp_test.c src/lexer.c src/ast.c src/parser.c src/semantic.c src/tac.c src/interp.c -Isrc -lm
./tests/interp_test sample1.matlang
```

Note the `-lm` flag on the last one, it links the math library, required since the execution engine uses `fabs()` for matrix inversion.

To see error handling in action, run any of the above against `sample2_bad.matlang` instead. It contains a dimension mismatch and should exit with a semantic error and line number instead of crashing.

## Status

Working: lexer, parser, AST, semantic analysis with symbol table, three-address code generation, and an execution engine that performs real matrix arithmetic including inversion via Gauss-Jordan elimination.

Not yet implemented: matrix chain multiplication optimization (currently `multiply(A, B, C, ...)` folds left to right with no cost optimization) and standalone code generation to an output file.
