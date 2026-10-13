/*
 * diag.c : prints an error message together with the source line and a caret.
 *
 * The steps for one error are:
 *   1. print the message line:  "Parse error at line L, column C: <text>"
 *   2. find line L inside the saved program text
 *   3. print that line with its line number in front
 *   4. print a second line with spaces up to column C, then ^ and ~~~ for the
 *      rest of the bad token
 *
 * Step 4 needs care. Tabs must be copied as tabs (otherwise the caret would
 * drift left of the real spot) and a character made of several bytes (UTF-8)
 * must count as ONE position, not one per byte.
 */
#include <stdlib.h>
#include <string.h>
#include "diag.h"

/* Our own copy of the whole program text. We copy it so the error printer
 * still works even if the caller frees or changes the original string. */
static char *source;

/* Saves a fresh copy of the program text, freeing the previous copy first.
 * If malloc fails, source stays NULL and errors are printed without the
 * source line (still safe, only less pretty). */
void diag_set_source(const char *src) {
    free(source);
    source = NULL;
    if (src) {
        size_t n = strlen(src) + 1;   /* +1 to copy the ending '\0' too */
        source = malloc(n);
        if (source) memcpy(source, src, n);
    }
}

/* Returns a pointer to the start of line number `line` (counting from 1)
 * inside the saved text, or NULL if there is no such line. It simply skips
 * (line - 1) new line characters. */
static const char *find_line(int line) {
    if (!source || line < 1) return NULL;
    const char *p = source;
    for (int n = 1; n < line; n++) {
        p = strchr(p, '\n');
        if (!p) return NULL;   /* the text has fewer lines than asked for */
        p++;                   /* step over the '\n' to the next line's start */
    }
    return p;
}

/* How many decimal digits a number has (digits(7)=1, digits(120)=3).
 * Used to keep the "|" bar of the line number column lined up. */
static int digits(int n) {
    int d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

/* Prints one complete error, message plus (when possible) the source line
 * and the caret line. See diag.h for the meaning of the arguments. */
void diag_print(FILE *out, const char *kind, int line, int col, int len, const char *msg) {
    /* first line: the message. The column is only shown when it is known. */
    if (col > 0) fprintf(out, "%s error at line %d, column %d: %s\n", kind, line, col, msg);
    else         fprintf(out, "%s error at line %d: %s\n", kind, line, msg);

    /* Without a known column or without the line text there is nothing to
     * point at, so we stop here. */
    const char *text = find_line(line);
    if (col <= 0 || !text) return;

    /* length of the line, not counting the new line (or a Windows \r before it) */
    int text_len = 0;
    while (text[text_len] && text[text_len] != '\n') text_len++;
    if (text_len > 0 && text[text_len - 1] == '\r') text_len--;
    if (text_len == 0 && col > 1) return;  /* nothing to show above the caret */

    /* the source line, with its number right aligned in a column of width w */
    int w = digits(line);
    fprintf(out, " %*d | %.*s\n", w, line, text_len, text);

    /* pad with the line's own tabs so the caret lines up, one space per
     * character (bytes that continue a multi-byte character count as zero) */
    fprintf(out, " %*s | ", w, "");
    for (int i = 0; i < col - 1; i++) {
        if (i < text_len) {
            unsigned char c = (unsigned char)text[i];
            if (c == '\t') fputc('\t', out);
            else if ((c & 0xC0) != 0x80) fputc(' ', out);  /* 10xxxxxx = continuation byte, skip */
        } else {
            fputc(' ', out);   /* column is past the end of the line (e.g. error at end of line) */
        }
    }
    fputc('^', out);
    /* the rest of the span, one ~ per character and kept inside the line */
    for (int i = 1; i < len && col - 1 + i < text_len; i++) {
        if (((unsigned char)text[col - 1 + i] & 0xC0) != 0x80) fputc('~', out);
    }
    fputc('\n', out);
}
