#include <stdlib.h>
#include <string.h>
#include "diag.h"

static char *source;

void diag_set_source(const char *src) {
    free(source);
    source = NULL;
    if (src) {
        size_t n = strlen(src) + 1;
        source = malloc(n);
        if (source) memcpy(source, src, n);
    }
}

/* start of line number `line` (1-based) inside source, or NULL */
static const char *find_line(int line) {
    if (!source || line < 1) return NULL;
    const char *p = source;
    for (int n = 1; n < line; n++) {
        p = strchr(p, '\n');
        if (!p) return NULL;
        p++;
    }
    return p;
}

static int digits(int n) {
    int d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

void diag_print(FILE *out, const char *kind, int line, int col, int len, const char *msg) {
    if (col > 0) fprintf(out, "%s error at line %d, column %d: %s\n", kind, line, col, msg);
    else         fprintf(out, "%s error at line %d: %s\n", kind, line, msg);

    const char *text = find_line(line);
    if (col <= 0 || !text) return;

    int text_len = 0;
    while (text[text_len] && text[text_len] != '\n') text_len++;
    if (text_len > 0 && text[text_len - 1] == '\r') text_len--;
    if (text_len == 0 && col > 1) return;  /* nothing to show above the caret */

    int w = digits(line);
    fprintf(out, " %*d | %.*s\n", w, line, text_len, text);

    /* pad with the line's own tabs so the caret lines up, one space per
     * character (bytes that continue a multi-byte character count as zero) */
    fprintf(out, " %*s | ", w, "");
    for (int i = 0; i < col - 1; i++) {
        if (i < text_len) {
            unsigned char c = (unsigned char)text[i];
            if (c == '\t') fputc('\t', out);
            else if ((c & 0xC0) != 0x80) fputc(' ', out);
        } else {
            fputc(' ', out);
        }
    }
    fputc('^', out);
    /* the rest of the span, one ~ per character and kept inside the line */
    for (int i = 1; i < len && col - 1 + i < text_len; i++) {
        if (((unsigned char)text[col - 1 + i] & 0xC0) != 0x80) fputc('~', out);
    }
    fputc('\n', out);
}
