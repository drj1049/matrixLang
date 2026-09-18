/* Proves lexer.c + ast.c + parser.c + semantic.c all link and work together.
 * Not the final main.c -- optimizer/codegen still need to be added.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/semantic.h"

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

    semantic_check(&prog); /* exits with a message if anything is invalid */

    printf("Semantic check passed. Annotated AST (dims shown as -> RxC):\n\n");
    for (int i = 0; i < prog.count; i++) {
        ast_print(prog.statements[i], 0);
        printf("\n");
    }

    program_free(&prog);
    free(source);
    return 0;
}