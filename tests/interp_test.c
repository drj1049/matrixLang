/* Full pipeline test: source -> tokens -> AST -> semantic check ->
 * optimizer -> TAC -> actual execution. This is the one to run for your
 * live demo, since it's the only stage that prints real computed numbers
 * instead of structure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/semantic.h"
#include "../src/optimizer.h"
#include "../src/tac.h"
#include "../src/tacopt.h"
#include "../src/interp.h"

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
    int optimize = 1;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O0") == 0) optimize = 0;
        else if (strcmp(argv[i], "-O1") == 0) optimize = 1;
        else if (!path) path = argv[i];
        else path = NULL, i = argc;   /* a second file name: show the usage */
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-O0|-O1] <file.matlang>\n", argv[0]);
        return 1;
    }

    char *source = read_file(path);
    Program prog = parse_program(source);
    semantic_check(&prog);
    if (optimize) optimize_program(&prog);
    TACProgram tac = tac_generate(&prog);
    if (optimize) tac_optimize(&tac, NULL);

    interp_run(&tac);

    tac_free(&tac);
    program_free(&prog);
    free(source);
    return 0;
}