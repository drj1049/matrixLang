/* Compiler driver: source -> tokens -> AST -> semantic check -> optimizer
 * -> TAC -> C code written to a file.
 * usage: codegen_test <file.matlang> <output.c>
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/semantic.h"
#include "../src/optimizer.h"
#include "../src/tac.h"
#include "../src/codegen.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file.matlang> <output.c>\n", argv[0]);
        return 1;
    }

    char *source = read_file(argv[1]);
    Program prog = parse_program(source);
    semantic_check(&prog);
    optimize_program(&prog);
    TACProgram tac = tac_generate(&prog);

    FILE *out = fopen(argv[2], "w");
    if (!out) { perror("fopen"); return 1; }
    codegen_c(&tac, out);
    fclose(out);

    tac_free(&tac);
    program_free(&prog);
    free(source);
    return 0;
}