/* Compiler driver: source -> tokens -> AST -> semantic check -> optimizer
 * -> TAC -> C code written to a file.
 * usage: codegen_test <file.matlang> <output.c> [function_name]
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
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file.matlang> <output.c> [function_name]\n", argv[0]);
        return 1;
    }
    const char *func_name = argc > 3 ? argv[3] : "matlang_compute";
    if (!is_c_identifier(func_name)) {
        fprintf(stderr, "error: '%s' is not a valid C function name\n", func_name);
        return 1;
    }

    char *source = read_file(argv[1]);
    Program prog = parse_program(source);
    semantic_check(&prog);
    optimize_program(&prog);
    TACProgram tac = tac_generate(&prog);

    FILE *out = fopen(argv[2], "w");
    if (!out) { perror("fopen"); return 1; }
    codegen_c(&tac, func_name, out);
    fclose(out);

    if (codegen_is_function(&tac)) {
        char *h_path = header_path_for(argv[2]);
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