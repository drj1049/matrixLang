/* Temporary driver just to prove lexer.h/lexer.c are wired correctly.
 * This is NOT the final main.c -- once parser/semantic/optimizer/codegen
 * exist, main.c will call lex() as one stage in the full pipeline instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/lexer.h"

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
    lex_init(source);

    Token t;
    do {
        t = lex_advance();
        printf("line %-3d %-10s '%s'\n", t.line, token_type_name(t.type), t.text);
    } while (t.type != TOK_EOF);

    free(source);
    return 0;
}