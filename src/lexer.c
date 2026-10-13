/*
 * lexer.c : turns the program text into tokens (see lexer.h for the idea).
 *
 * HOW IT WORKS
 * The lexer keeps a pointer (src_ptr) that walks through the source string one
 * character at a time. To produce the next token it:
 *   1. skips spaces, tabs, new lines and # comments,
 *   2. looks at the first character to decide what kind of token starts there
 *        letter or _  -> a name (or a keyword)
 *        digit        -> a number (or a size like 2x3)
 *        anything else -> a one character token such as + or [
 *   3. reads as many characters as belong to that token and returns it.
 *
 * It also remembers the line number and where the line started, so that every
 * token knows its line and column. That is what lets the compiler draw a ^
 * under the exact spot of an error.
 *
 * ONE TOKEN OF LOOKAHEAD
 * The parser wants to peek at the next token before deciding what to do. So
 * the lexer scans one token ahead and keeps it in a small buffer
 * (cached_token). lex_peek() fills the buffer if it is empty, lex_advance()
 * hands the buffered token out and empties it.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

/* ---- Internal lexer state ----
 * These are file-private (static) variables, so only this file can touch them.
 * That is why lex_init() must be called before starting a new program. */
static const char *src_ptr;   /* current read position in source */
static int current_line;      /* line the lexer is on now, counting from 1 */
static const char *line_start; /* where the current line begins, to work out columns */
static Token cached_token;    /* one-token lookahead buffer */
static int has_cached;        /* whether cached_token is valid */

/* Keyword table: text the user types -> token type.
 * Checked after we've already scanned a whole identifier: a word is first read
 * as a name, then looked up here. If it is in the table it becomes a keyword
 * token instead of a plain TOK_ID.
 */
typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"add",       TOK_ADD},
    {"subtract",  TOK_SUBTRACT},
    {"multiply",  TOK_MULTIPLY},
    {"transpose", TOK_TRANSPOSE},
    {"inverse",   TOK_INVERSE},
    {"identity",  TOK_IDENTITY},
    {"zeros",     TOK_ZEROS},
    {"print",     TOK_PRINT},
    {"input",     TOK_INPUT},
    {"output",    TOK_OUTPUT},
};
/* number of entries, worked out by the compiler so it never goes out of date */
static const int NUM_KEYWORDS = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

/* Is this word a keyword? Returns its token type, or TOK_ID if it is just a
 * normal variable name. A linear search is fine for ten words. */
static TokenType keyword_lookup(const char *text) {
    for (int i = 0; i < NUM_KEYWORDS; i++) {
        if (strcmp(text, KEYWORDS[i].word) == 0) {
            return KEYWORDS[i].type;
        }
    }
    return TOK_ID; /* not a keyword -> plain identifier */
}

/* Moves src_ptr forward over everything that is not part of any token:
 * spaces, tabs, carriage returns, new lines and # comments. Every time it
 * passes a new line it bumps the line counter and notes where the next line
 * starts (needed later to compute columns). */
static void skip_whitespace_and_comments(void) {
    for (;;) {
        char c = *src_ptr;
        if (c == '\n') {
            current_line++;
            src_ptr++;
            line_start = src_ptr;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            src_ptr++;
        } else if (c == '#') {
            /* '#' starts a comment to end of line. We stop AT the new line
             * (not after it) so the branch above counts it. */
            while (*src_ptr != '\0' && *src_ptr != '\n') src_ptr++;
        } else {
            break;
        }
    }
}

/* Builds a Token. `start` points at its first character and `len` is how many
 * characters it has. The column is the distance from the start of the line,
 * plus 1 because columns count from 1 (like editors show them). */
static Token make_token(TokenType type, const char *start, int len) {
    Token t;
    t.type = type;
    t.line = current_line;
    t.col = (int)(start - line_start) + 1;
    t.len = len;
    /* keep the real length in t.len (for the ^~~~ underline) but never copy
     * more text than the buffer can hold */
    if (len >= TOKEN_TEXT_MAX) len = TOKEN_TEXT_MAX - 1;
    memcpy(t.text, start, len);
    t.text[len] = '\0';
    return t;
}

/* Reads a name: a letter or _ followed by letters, digits or _ . Then checks
 * whether the word is a keyword (add, print, ...) and sets the type to match. */
static Token scan_identifier_or_keyword(void) {
    const char *start = src_ptr;
    while (isalnum((unsigned char)*src_ptr) || *src_ptr == '_') {
        src_ptr++;
    }
    int len = (int)(src_ptr - start);
    Token t = make_token(TOK_ID, start, len);
    t.type = keyword_lookup(t.text);
    return t;
}

/* Reads something that starts with a digit. Three cases:
 *   2x3   -> TOK_DIMS   (a size, used by "input A 2x3")
 *   3.14  -> TOK_NUMBER (digits, a dot, digits)
 *   42    -> TOK_NUMBER
 * Negative numbers are not handled here: the minus sign is its own token and
 * the parser puts it together with the number. */
static Token scan_number(void) {
    const char *start = src_ptr;
    while (isdigit((unsigned char)*src_ptr)) src_ptr++;
    /* digits 'x' digits is a matrix size, e.g. 2x3 */
    if (*src_ptr == 'x' && isdigit((unsigned char)src_ptr[1])) {
        src_ptr++;
        while (isdigit((unsigned char)*src_ptr)) src_ptr++;
        return make_token(TOK_DIMS, start, (int)(src_ptr - start));
    }
    /* a dot only counts if a digit follows it, so "3." is the number 3
     * followed by an unknown character */
    if (*src_ptr == '.' && isdigit((unsigned char)src_ptr[1])) {
        src_ptr++;
        while (isdigit((unsigned char)*src_ptr)) src_ptr++;
    }
    int len = (int)(src_ptr - start);
    return make_token(TOK_NUMBER, start, len);
}

/* Scans the next token straight from the text (no buffering, that is done by
 * lex_peek below). Decides the kind of token from its first character. */
static Token scan_next_raw(void) {
    skip_whitespace_and_comments();

    /* the string ends with '\0': no more tokens, so give the end marker */
    if (*src_ptr == '\0') {
        return make_token(TOK_EOF, src_ptr, 0);
    }

    char c = *src_ptr;

    if (isalpha((unsigned char)c) || c == '_') {
        return scan_identifier_or_keyword();
    }
    if (isdigit((unsigned char)c)) {
        return scan_number();
    }

    /* single-character tokens: take the character, then pick its kind */
    const char *start = src_ptr;
    src_ptr++;
    switch (c) {
        case '+': return make_token(TOK_PLUS, start, 1);
        case '-': return make_token(TOK_MINUS, start, 1);
        case '*': return make_token(TOK_STAR, start, 1);
        case '=': return make_token(TOK_EQUALS, start, 1);
        case '(': return make_token(TOK_LPAREN, start, 1);
        case ')': return make_token(TOK_RPAREN, start, 1);
        case '[': return make_token(TOK_LBRACKET, start, 1);
        case ']': return make_token(TOK_RBRACKET, start, 1);
        case ',': return make_token(TOK_COMMA, start, 1);
        case ';': return make_token(TOK_SEMICOLON, start, 1);
        default:
            /* keep a multi-byte character (UTF-8) together as one unknown token,
             * so an error message never shows half of a character.
             * (0xC0 and above starts a multi-byte character, bytes that look
             * like 10xxxxxx are its continuation bytes) */
            if ((unsigned char)c >= 0xC0) {
                while (((unsigned char)*src_ptr & 0xC0) == 0x80) src_ptr++;
            }
            return make_token(TOK_UNKNOWN, start, (int)(src_ptr - start));
    }
}

/* Starts scanning a new program: go back to the beginning, line 1, and throw
 * away any token left in the lookahead buffer. */
void lex_init(const char *src) {
    src_ptr = src;
    current_line = 1;
    line_start = src;
    has_cached = 0;
}

/* Returns the next token without using it up. The token is scanned the first
 * time and then kept in the buffer, so peeking many times is cheap and always
 * gives the same answer. */
Token lex_peek(void) {
    if (!has_cached) {
        cached_token = scan_next_raw();
        has_cached = 1;
    }
    return cached_token;
}

/* Returns the next token and moves past it (the buffer is emptied, so the
 * next peek scans a fresh token). */
Token lex_advance(void) {
    Token t = lex_peek();
    has_cached = 0;
    return t;
}

/* The name of a token kind as text, used by the lexer test program that prints
 * every token. */
const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_EOF:        return "EOF";
        case TOK_ID:         return "ID";
        case TOK_NUMBER:     return "NUMBER";
        case TOK_DIMS:       return "DIMS";
        case TOK_PLUS:       return "PLUS";
        case TOK_MINUS:      return "MINUS";
        case TOK_STAR:       return "STAR";
        case TOK_EQUALS:     return "EQUALS";
        case TOK_LPAREN:     return "LPAREN";
        case TOK_RPAREN:     return "RPAREN";
        case TOK_LBRACKET:   return "LBRACKET";
        case TOK_RBRACKET:   return "RBRACKET";
        case TOK_COMMA:      return "COMMA";
        case TOK_SEMICOLON:  return "SEMICOLON";
        case TOK_ADD:        return "ADD";
        case TOK_SUBTRACT:   return "SUBTRACT";
        case TOK_MULTIPLY:   return "MULTIPLY";
        case TOK_TRANSPOSE:  return "TRANSPOSE";
        case TOK_INVERSE:    return "INVERSE";
        case TOK_IDENTITY:   return "IDENTITY";
        case TOK_ZEROS:      return "ZEROS";
        case TOK_PRINT:      return "PRINT";
        case TOK_INPUT:      return "INPUT";
        case TOK_OUTPUT:     return "OUTPUT";
        default:             return "UNKNOWN";
    }
}
