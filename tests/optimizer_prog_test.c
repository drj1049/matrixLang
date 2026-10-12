#include <stdio.h>
#include <stdlib.h>
#include "../src/parser.h"
#include "../src/semantic.h"
#include "../src/optimizer.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.matlang\n", argv[0]);
        return 1;
    }
    char *src = read_file(argv[1]);
    Program prog = parse_program(src);
    semantic_check(&prog);

    printf("=== before ===\n");
    for (int i = 0; i < prog.count; i++) ast_print(prog.statements[i], 0);

    optimize_program(&prog);

    printf("=== after ===\n");
    for (int i = 0; i < prog.count; i++) ast_print(prog.statements[i], 0);

    program_free(&prog);
    free(src);
    return 0;
}