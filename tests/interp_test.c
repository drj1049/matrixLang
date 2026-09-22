/* Full pipeline test: source -> tokens -> AST -> semantic check -> TAC ->
 * actual execution. This is the one to run for your live demo, since it's
 * the only stage that prints real computed numbers instead of structure.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/semantic.h"
#include "../src/tac.h"
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
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.matlang>\n", argv[0]);
        return 1;
    }

    char *source = read_file(argv[1]);
    Program prog = parse_program(source);
    semantic_check(&prog);
    TACProgram tac = tac_generate(&prog);

    interp_run(&tac);

    tac_free(&tac);
    program_free(&prog);
    free(source);
    return 0;
}