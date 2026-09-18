/* Temporary driver, same idea as lexer_test.c -- proves parser.c, ast.c,
 * and lexer.c all link together and produce a correct AST. Not the final
 * main.c; that comes once semantic/optimizer/codegen exist too.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/parser.h"

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
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.matlang>\n", argv[0]);
        return 1;
    }

    char *source = read_file(argv[1]);
    Program prog = parse_program(source);

    printf("Parsed %d statement(s):\n\n", prog.count);
    for (int i = 0; i < prog.count; i++) {
        ast_print(prog.statements[i], 0);
        printf("\n");
    }

    program_free(&prog);
    free(source);
    return 0;
}