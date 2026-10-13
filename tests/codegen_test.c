/* Compiler driver: source -> tokens -> AST -> semantic check -> optimizer
 * -> TAC -> C code written to a file.
 * usage: codegen_test [-O0|-O1] <file.matlang> <output.c> [function_name]
 *
 * -O1 (the default) runs the matrix chain optimizer; -O0 skips it, so the
 * multiplications are compiled in exactly the order they were written.
 *
 * A program with input/output statements becomes a C function (named
 * function_name, default matlang_compute) plus a header next to output.c
 * that declares it. Any other program becomes a main() as before.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../src/semantic.h"
#include "../src/optimizer.h"
#include "../src/tac.h"
#include "../src/tacopt.h"
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

static int is_c_identifier(const char *s) {
    if (!isalpha((unsigned char)*s) && *s != '_') return 0;
    for (; *s; s++) {
        if (!isalnum((unsigned char)*s) && *s != '_') return 0;
    }
    return 1;
}

/* build/foo.c -> build/foo.h (anything not ending in .c just gets .h appended) */
static char *header_path_for(const char *c_path) {
    size_t len = strlen(c_path);
    char *h = malloc(len + 3);
    strcpy(h, c_path);
    if (len >= 2 && strcmp(c_path + len - 2, ".c") == 0) h[len - 1] = 'h';
    else strcat(h, ".h");
    return h;
}

int main(int argc, char **argv) {
    int optimize = 1;
    const char *pos[3];
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O0") == 0) optimize = 0;
        else if (strcmp(argv[i], "-O1") == 0) optimize = 1;
        else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "error: unknown option '%s' (the options are -O0 and -O1)\n", argv[i]);
            return 1;
        }
        else if (npos < 3) pos[npos++] = argv[i];
        else npos = 4;
    }
    if (npos < 2 || npos > 3) {
        fprintf(stderr, "usage: %s [-O0|-O1] <file.matlang> <output.c> [function_name]\n", argv[0]);
        return 1;
    }
    const char *func_name = npos > 2 ? pos[2] : "matlang_compute";
    if (!is_c_identifier(func_name)) {
        fprintf(stderr, "error: '%s' is not a valid C function name\n", func_name);
        return 1;
    }

    char *source = read_file(pos[0]);
    Program prog = parse_program(source);
    semantic_check(&prog);
    if (optimize) optimize_program(&prog);
    TACProgram tac = tac_generate(&prog);
    if (optimize) tac_optimize(&tac, NULL);

    FILE *out = fopen(pos[1], "w");
    if (!out) { perror("fopen"); return 1; }
    codegen_c(&tac, func_name, out);
    fclose(out);

    if (codegen_is_function(&tac)) {
        char *h_path = header_path_for(pos[1]);
        FILE *h = fopen(h_path, "w");
        if (!h) { perror("fopen"); return 1; }
        codegen_header(&tac, func_name, h);
        fclose(h);
        free(h_path);
    }

    tac_free(&tac);
    program_free(&prog);
    free(source);
    return 0;
}