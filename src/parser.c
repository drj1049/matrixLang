#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"

/* ---- Error helper ---- */
static void parse_error(const char *msg, Token got) {
    fprintf(stderr, "Parse error at line %d: %s (got '%s')\n", got.line, msg, got.text);
    exit(1);
}

/* Consumes the current token if it matches `type`, else reports an error. */
static Token expect(TokenType type, const char *what) {
    Token t = lex_advance();
    if (t.type != type) parse_error(what, t);
    return t;
}

/* ---- Forward declarations for the recursive-descent functions ---- */
static ASTNode *parse_expr(void);      /* term (('+'|'-') term)* */
static ASTNode *parse_term(void);      /* factor ('*' factor)* */
static ASTNode *parse_factor(void);    /* ID | matrixLiteral */
static ASTNode *parse_matrix_literal(void);
static ASTNode *parse_func_call(void);
static ASTNode *parse_statement(void);

static int is_func_keyword(TokenType t) {
    return t == TOK_ADD || t == TOK_SUBTRACT || t == TOK_MULTIPLY ||
           t == TOK_TRANSPOSE || t == TOK_INVERSE;
}

/* ---- matrixLiteral -> '[' row (',' row)* ']', row -> '[' NUMBER (',' NUMBER)* ']'
 * Reads one row at a time into a growable buffer, all rows must have the
 * same column count (checked here, since a ragged matrix is never valid).
 */
static ASTNode *parse_matrix_literal(void) {
    int line = lex_peek().line;
    expect(TOK_LBRACKET, "expected '[' to start matrix literal");

    double **rows = NULL;
    int row_count = 0;
    int col_count = -1; /* unset until first row */

    do {
        expect(TOK_LBRACKET, "expected '[' to start a matrix row");

        double *row_values = NULL;
        int this_row_cols = 0;

        do {
            Token num = expect(TOK_NUMBER, "expected a number inside matrix row");
            this_row_cols++;
            row_values = realloc(row_values, sizeof(double) * this_row_cols);
            row_values[this_row_cols - 1] = atof(num.text);
        } while (lex_peek().type == TOK_COMMA && (lex_advance(), 1));

        expect(TOK_RBRACKET, "expected ']' to close matrix row");

        if (col_count == -1) {
            col_count = this_row_cols;
        } else if (this_row_cols != col_count) {
            fprintf(stderr, "Parse error at line %d: matrix rows have inconsistent lengths\n", line);
            exit(1);
        }

        row_count++;
        rows = realloc(rows, sizeof(double *) * row_count);
        rows[row_count - 1] = row_values;

    } while (lex_peek().type == TOK_COMMA && (lex_advance(), 1));

    expect(TOK_RBRACKET, "expected ']' to close matrix literal");

    return ast_new_matrix_literal(rows, row_count, col_count, line);
}

/* factor -> ID | matrixLiteral */
static ASTNode *parse_factor(void) {
    Token t = lex_peek();
    if (t.type == TOK_ID) {
        lex_advance();
        return ast_new_identifier(t.text, t.line);
    }
    if (t.type == TOK_LBRACKET) {
        return parse_matrix_literal();
    }
    parse_error("expected an identifier or matrix literal", t);
    return NULL; /* unreachable, silences compiler warning */
}

/* term -> factor ('*' factor)* */
static ASTNode *parse_term(void) {
    ASTNode *left = parse_factor();
    while (lex_peek().type == TOK_STAR) {
        Token op = lex_advance();
        ASTNode *right = parse_factor();
        left = ast_new_binop('*', left, right, op.line);
    }
    return left;
}

/* expr -> term (('+'|'-') term)* */
static ASTNode *parse_expr(void) {
    ASTNode *left = parse_term();
    while (lex_peek().type == TOK_PLUS || lex_peek().type == TOK_MINUS) {
        Token op = lex_advance();
        ASTNode *right = parse_term();
        char op_char = (op.type == TOK_PLUS) ? '+' : '-';
        left = ast_new_binop(op_char, left, right, op.line);
    }
    return left;
}

/* funcCall -> (ADD|SUBTRACT|MULTIPLY) '(' argList ')'
 *           | (TRANSPOSE|INVERSE) '(' ID ')'
 * argList  -> ID (',' ID)*
 * add/subtract/multiply need 2+ args; transpose/inverse need exactly 1 --
 * enforced here since it's part of the grammar shape, not just semantics.
 */
static ASTNode *parse_func_call(void) {
    Token kw = lex_advance(); /* the keyword token itself, e.g. TOK_ADD */
    expect(TOK_LPAREN, "expected '(' after function name");

    ASTNode **args = NULL;
    int arg_count = 0;

    do {
        Token id = expect(TOK_ID, "expected an identifier argument");
        arg_count++;
        args = realloc(args, sizeof(ASTNode *) * arg_count);
        args[arg_count - 1] = ast_new_identifier(id.text, id.line);
    } while (lex_peek().type == TOK_COMMA && (lex_advance(), 1));

    expect(TOK_RPAREN, "expected ')' to close function call");

    if ((kw.type == TOK_TRANSPOSE || kw.type == TOK_INVERSE) && arg_count != 1) {
        fprintf(stderr, "Parse error at line %d: %s takes exactly 1 argument, got %d\n",
                kw.line, kw.text, arg_count);
        exit(1);
    }
    if ((kw.type == TOK_ADD || kw.type == TOK_SUBTRACT || kw.type == TOK_MULTIPLY) && arg_count < 2) {
        fprintf(stderr, "Parse error at line %d: %s needs at least 2 arguments, got %d\n",
                kw.line, kw.text, arg_count);
        exit(1);
    }

    return ast_new_func_call(kw.type, args, arg_count, kw.line);
}

/* statement -> ID '=' (funcCall | expr)
 *            | PRINT '(' ID ')'
 */
static ASTNode *parse_statement(void) {
    Token t = lex_peek();

    if (t.type == TOK_PRINT) {
        lex_advance();
        expect(TOK_LPAREN, "expected '(' after print");
        Token id = expect(TOK_ID, "expected an identifier inside print(...)");
        expect(TOK_RPAREN, "expected ')' to close print(...)");
        return ast_new_print(id.text, t.line);
    }

    if (t.type == TOK_ID) {
        Token target = lex_advance();
        expect(TOK_EQUALS, "expected '=' after identifier");

        ASTNode *value = is_func_keyword(lex_peek().type)
                              ? parse_func_call()
                              : parse_expr();

        return ast_new_assign(target.text, value, target.line);
    }

    parse_error("expected a statement (assignment or print)", t);
    return NULL; /* unreachable */
}

/* program -> statement* (until EOF) */
Program parse_program(const char *src) {
    lex_init(src);

    Program prog = { NULL, 0 };
    while (lex_peek().type != TOK_EOF) {
        prog.count++;
        prog.statements = realloc(prog.statements, sizeof(ASTNode *) * prog.count);
        prog.statements[prog.count - 1] = parse_statement();
    }
    return prog;
}

void program_free(Program *prog) {
    for (int i = 0; i < prog->count; i++) {
        ast_free(prog->statements[i]);
    }
    free(prog->statements);
    prog->statements = NULL;
    prog->count = 0;
}