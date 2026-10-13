#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include "parser.h"
#include "lexer.h"

/* ---- Error handling ----
 * A syntax error does not stop the compiler straight away. It is written
 * down, the parser jumps back to the statement loop (setjmp/longjmp), skips
 * ahead to the start of the next statement ("panic mode" recovery) and keeps
 * going, so one run can report several mistakes. When the whole program has
 * been read, every error found is printed and the compiler exits.
 * (A statement that failed may leave half-built nodes behind. They are not
 * freed, because the compiler exits right after reporting.)
 */
#define MAX_ERRORS 20

typedef struct {
    int line;
    char msg[256];
} ParseError;

static ParseError errors[MAX_ERRORS];
static int error_count;
static int too_many_errors;
static jmp_buf stmt_jmp;
static int stmt_start_line;   /* line where the statement being parsed began */

static void fail_at(int line, const char *fmt, ...) {
    if (error_count < MAX_ERRORS) {
        va_list args;
        va_start(args, fmt);
        errors[error_count].line = line;
        vsnprintf(errors[error_count].msg, sizeof errors[error_count].msg, fmt, args);
        va_end(args);
        error_count++;
    } else {
        too_many_errors = 1;
    }
    longjmp(stmt_jmp, 1);
}

static void parse_error(const char *msg, Token got) {
    if (got.type == TOK_EOF) fail_at(got.line, "%s (reached the end of the program)", msg);
    else                     fail_at(got.line, "%s (got '%s')", msg, got.text);
}

/* Consumes the current token if it matches `type`, else reports an error.
 * A wrong token is left in place, so recovery can look at it. */
static Token expect(TokenType type, const char *what) {
    Token t = lex_peek();
    if (t.type != type) parse_error(what, t);
    return lex_advance();
}

/* ---- Forward declarations for the recursive-descent functions ---- */
static ASTNode *parse_expr(void);      /* term (('+'|'-') term)* */
static ASTNode *parse_term(void);      /* unary ('*' unary)* */
static ASTNode *parse_unary(void);     /* '-' unary | NUMBER | factor */
static ASTNode *parse_factor(void);    /* ID | matrixLiteral | matrixGen */
static ASTNode *parse_matrix_literal(void);
static ASTNode *parse_matrix_gen(void);
static ASTNode *parse_func_call(void);
static ASTNode *parse_statement(void);

static int is_func_keyword(TokenType t) {
    return t == TOK_ADD || t == TOK_SUBTRACT || t == TOK_MULTIPLY ||
           t == TOK_TRANSPOSE || t == TOK_INVERSE;
}

/* ---- matrixLiteral -> '[' row (',' row)* ']', row -> '[' value (',' value)* ']'
 * value -> ['-'] NUMBER
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
            int negative = 0;
            if (lex_peek().type == TOK_MINUS) {
                lex_advance();
                negative = 1;
            }
            Token num = expect(TOK_NUMBER, "expected a number inside matrix row");
            double value = atof(num.text);
            this_row_cols++;
            row_values = realloc(row_values, sizeof(double) * this_row_cols);
            /* "-0" stays 0, so it never prints as -0 */
            row_values[this_row_cols - 1] = (negative && value != 0) ? -value : value;
        } while (lex_peek().type == TOK_COMMA && (lex_advance(), 1));

        expect(TOK_RBRACKET, "expected ']' to close matrix row");

        if (col_count == -1) {
            col_count = this_row_cols;
        } else if (this_row_cols != col_count) {
            fail_at(line, "matrix rows have inconsistent lengths");
        }

        row_count++;
        rows = realloc(rows, sizeof(double *) * row_count);
        rows[row_count - 1] = row_values;

    } while (lex_peek().type == TOK_COMMA && (lex_advance(), 1));

    expect(TOK_RBRACKET, "expected ']' to close matrix literal");

    return ast_new_matrix_literal(rows, row_count, col_count, line);
}

/* One size inside identity(...) / zeros(...): a whole number from 1 up to
 * MAX_GEN_DIM. */
#define MAX_GEN_DIM 10000
#define MAX_GEN_CELLS 10000000L

static int parse_size(const char *what) {
    Token t = expect(TOK_NUMBER, what);
    if (strchr(t.text, '.')) {
        fail_at(t.line, "a size must be a whole number (got '%s')", t.text);
    }
    long v = strtol(t.text, NULL, 10);
    if (v < 1 || v > MAX_GEN_DIM) {
        fail_at(t.line, "a size must be between 1 and %d (got '%s')", MAX_GEN_DIM, t.text);
    }
    return (int)v;
}

/* matrixGen -> IDENTITY '(' NUMBER ')' | ZEROS '(' NUMBER ',' NUMBER ')' */
static ASTNode *parse_matrix_gen(void) {
    Token kw = lex_advance();
    expect(TOK_LPAREN, "expected '(' after function name");

    int rows, cols;
    if (kw.type == TOK_IDENTITY) {
        rows = cols = parse_size("expected the size n in identity(n)");
    } else {
        rows = parse_size("expected the number of rows in zeros(rows, cols)");
        expect(TOK_COMMA, "expected ',' between the rows and columns in zeros(rows, cols)");
        cols = parse_size("expected the number of columns in zeros(rows, cols)");
    }
    expect(TOK_RPAREN, "expected ')' to close function call");

    if ((long)rows * cols > MAX_GEN_CELLS) {
        fail_at(kw.line, "a %dx%d matrix is too large (limit is %ld elements)", rows, cols, MAX_GEN_CELLS);
    }
    return ast_new_matrix_gen(kw.type, rows, cols, kw.line);
}

/* factor -> ID | matrixLiteral | matrixGen */
static ASTNode *parse_factor(void) {
    Token t = lex_peek();
    if (t.type == TOK_ID) {
        lex_advance();
        return ast_new_identifier(t.text, t.line);
    }
    if (t.type == TOK_LBRACKET) {
        return parse_matrix_literal();
    }
    if (t.type == TOK_IDENTITY || t.type == TOK_ZEROS) {
        return parse_matrix_gen();
    }
    parse_error("expected an identifier, matrix literal or number", t);
    return NULL; /* unreachable, silences compiler warning */
}

/* -0 is just 0, so a scaled matrix never prints "-0" because of the number */
static double clean_zero(double v) {
    return v == 0 ? 0.0 : v;
}

/* factor * matrix: scaling a scaled matrix just multiplies the two numbers */
static ASTNode *make_scale(double factor, ASTNode *matrix, int line) {
    if (matrix->type == NODE_SCALE) {
        matrix->number = clean_zero(matrix->number * factor);
        return matrix;
    }
    return ast_new_scale(clean_zero(factor), matrix, line);
}

/* '-' applied to a number gives a number, applied to a matrix it scales by -1 */
static ASTNode *negate(ASTNode *operand, int line) {
    if (operand->type == NODE_NUMBER) {
        operand->number = clean_zero(-operand->number);
        return operand;
    }
    return make_scale(-1.0, operand, line);
}

/* one '*' step. Number * number stays a number, number * matrix (either
 * way round) becomes a scale, matrix * matrix is a real matrix multiply. */
static ASTNode *make_product(ASTNode *left, ASTNode *right, int line) {
    if (left->type == NODE_NUMBER && right->type == NODE_NUMBER) {
        left->number = clean_zero(left->number * right->number);
        ast_free(right);
        return left;
    }
    if (left->type == NODE_NUMBER) {
        double k = left->number;
        ast_free(left);
        return make_scale(k, right, line);
    }
    if (right->type == NODE_NUMBER) {
        double k = right->number;
        ast_free(right);
        return make_scale(k, left, line);
    }
    return ast_new_binop('*', left, right, line);
}

/* unary -> '-' unary | NUMBER | factor */
static ASTNode *parse_unary(void) {
    Token t = lex_peek();
    if (t.type == TOK_MINUS) {
        lex_advance();
        return negate(parse_unary(), t.line);
    }
    if (t.type == TOK_NUMBER) {
        lex_advance();
        return ast_new_number(atof(t.text), t.line);
    }
    return parse_factor();
}

/* term -> unary ('*' unary)*
 * A number on its own is not a matrix, so a term must not end up as one. */
static ASTNode *parse_term(void) {
    int line = lex_peek().line;
    ASTNode *left = parse_unary();
    while (lex_peek().type == TOK_STAR) {
        Token op = lex_advance();
        ASTNode *right = parse_unary();
        left = make_product(left, right, op.line);
    }
    if (left->type == NODE_NUMBER) {
        fail_at(line, "a number must be multiplied with a matrix (for example 2 * A)");
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
        fail_at(kw.line, "%s takes exactly 1 argument, got %d", kw.text, arg_count);
    }
    if ((kw.type == TOK_ADD || kw.type == TOK_SUBTRACT || kw.type == TOK_MULTIPLY) && arg_count < 2) {
        fail_at(kw.line, "%s needs at least 2 arguments, got %d", kw.text, arg_count);
    }

    return ast_new_func_call(kw.type, args, arg_count, kw.line);
}

/* statement -> ID '=' (funcCall | expr)
 *            | PRINT '(' ID ')'
 *            | INPUT ID DIMS
 *            | OUTPUT ID
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

    if (t.type == TOK_INPUT) {
        lex_advance();
        Token id = expect(TOK_ID, "expected a matrix name after input");
        Token dims = expect(TOK_DIMS, "expected a size like 2x3 after the input name");

        /* the lexer guarantees the shape digits 'x' digits */
        char *end;
        long rows = strtol(dims.text, &end, 10);
        long cols = strtol(end + 1, NULL, 10);
        if (rows < 1 || cols < 1 || rows > INT_MAX || cols > INT_MAX / rows) {
            fail_at(dims.line, "invalid size '%s' for input '%s' "
                               "(rows and columns must be at least 1, and not too large)",
                    dims.text, id.text);
        }
        return ast_new_input(id.text, (int)rows, (int)cols, t.line);
    }

    if (t.type == TOK_OUTPUT) {
        lex_advance();
        Token id = expect(TOK_ID, "expected a matrix name after output");
        return ast_new_output(id.text, t.line);
    }

    if (t.type == TOK_ID) {
        Token target = lex_advance();
        expect(TOK_EQUALS, "expected '=' after identifier");

        ASTNode *value = is_func_keyword(lex_peek().type)
                              ? parse_func_call()
                              : parse_expr();

        return ast_new_assign(target.text, value, target.line);
    }

    if (t.type == TOK_EQUALS) {
        fail_at(t.line, "unexpected '=' (does the line above end with an operator?)");
    }
    parse_error("expected a statement (assignment, print, input or output)", t);
    return NULL; /* unreachable */
}

/* After an error: skip to the first token that can begin a statement. print,
 * input and output can only ever start one; a name on a later line than the
 * broken statement started on is taken as the start of the next one. */
static void skip_to_next_statement(void) {
    for (;;) {
        Token t = lex_peek();
        if (t.type == TOK_EOF) return;
        if (t.type == TOK_PRINT || t.type == TOK_INPUT || t.type == TOK_OUTPUT) return;
        if (t.type == TOK_ID && t.line > stmt_start_line) return;
        lex_advance();
    }
}

static void report_errors_and_exit(void) {
    for (int i = 0; i < error_count; i++) {
        fprintf(stderr, "Parse error at line %d: %s\n", errors[i].line, errors[i].msg);
    }
    if (too_many_errors) {
        fprintf(stderr, "Too many errors, stopping after %d.\n", MAX_ERRORS);
    } else if (error_count > 1) {
        fprintf(stderr, "%d errors found.\n", error_count);
    }
    exit(1);
}

/* program -> statement* (until EOF) */
Program parse_program(const char *src) {
    lex_init(src);
    error_count = 0;
    too_many_errors = 0;

    Program prog = { NULL, 0 };
    while (lex_peek().type != TOK_EOF) {
        stmt_start_line = lex_peek().line;
        if (setjmp(stmt_jmp) == 0) {
            ASTNode *stmt = parse_statement();
            prog.count++;
            prog.statements = realloc(prog.statements, sizeof(ASTNode *) * prog.count);
            prog.statements[prog.count - 1] = stmt;
        } else {
            if (too_many_errors) break;
            skip_to_next_statement();
        }
    }
    if (error_count > 0) report_errors_and_exit();
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