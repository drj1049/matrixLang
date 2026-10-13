/*
 * parser.c : the recursive-descent parser.
 *
 * THE GRAMMAR (one C function for each rule)
 *
 *   program   -> statement* EOF
 *   statement -> ID '=' (funcCall | expr)
 *              | 'print' '(' ID ')'
 *              | 'input' ID DIMS
 *              | 'output' ID
 *   expr      -> term (('+' | '-') term)*
 *   term      -> unary ('*' unary)*
 *   unary     -> '-' unary | NUMBER | factor
 *   factor    -> ID | matrixLiteral | matrixGen | '(' expr ')'
 *
 * WHY expr, term AND unary ARE SEPARATE RULES
 * The rule that is called deeper binds tighter. '*' lives in `term`, which
 * sits below `expr` where '+' and '-' live, so 1 + 2 * 3 reads as
 * 1 + (2 * 3). In the loops of expr and term, the tree grows to the left,
 * so A - B - C means (A - B) - C, as in normal maths.
 *
 * ERROR HANDLING (panic mode recovery)
 * When something is wrong, the parser must not just quit, because we want to
 * report several mistakes in one run. So:
 *   1. the error is written into an array (see vfail),
 *   2. longjmp jumps back to the loop in parse_program (the setjmp there),
 *   3. skip_to_next_statement throws tokens away until a new statement can
 *      start (this is called "panic mode"),
 *   4. parsing continues from that point.
 * At the end all collected errors are printed in order, and the compiler exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include "parser.h"
#include "lexer.h"
#include "diag.h"

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

/* One saved error: where it points and the message text. */
typedef struct {
    int line;
    int col;
    int len;       /* how many characters the ^~~~ underline should cover */
    char msg[256];
} ParseError;

/* All state below is file-private and reset at the start of parse_program. */
static ParseError errors[MAX_ERRORS];
static int error_count;
static int too_many_errors;   /* set when a 21st error shows up (it is not stored) */
static jmp_buf stmt_jmp;      /* the place longjmp returns to: the statement loop */
static int stmt_start_line;   /* line where the statement being parsed began */
static Token last_tok;        /* the token consumed most recently (line 0 if none yet) */
static int nest_depth;        /* how deep the unary minus / parenthesis nesting is right now */

#define MAX_NEST 200          /* keeps the recursive parser from overflowing the stack */
#define MAX_OPS 2000          /* operators in one statement: bounds how deep the tree gets */
static int stmt_ops;          /* operators seen so far in the statement being parsed */

/* the only place the parser consumes tokens, so last_tok is always right */
static Token advance(void) {
    last_tok = lex_advance();
    return last_tok;
}

/* write an error down (where it points and what it says), then jump back to
 * the statement loop. This function never returns normally: longjmp leaves it.
 * All the other error helpers below end up here. */
static void vfail(int line, int col, int len, const char *fmt, va_list args) {
    if (error_count < MAX_ERRORS) {
        errors[error_count].line = line;
        errors[error_count].col = col;
        errors[error_count].len = len;
        vsnprintf(errors[error_count].msg, sizeof errors[error_count].msg, fmt, args);
        error_count++;
    } else {
        /* no room left: just remember that errors were dropped */
        too_many_errors = 1;
    }
    longjmp(stmt_jmp, 1);
}

/* an error that points at one token */
static void fail_tok(Token t, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfail(t.line, t.col, t.len > 0 ? t.len : 1, fmt, args);
    va_end(args);
}

/* an error that points from `first` up to the token consumed last, when that
 * is all on one line */
static void fail_span(Token first, const char *fmt, ...) {
    int len = first.len;
    if (last_tok.line == first.line && last_tok.col + last_tok.len > first.col) {
        len = last_tok.col + last_tok.len - first.col;
    }
    va_list args;
    va_start(args, fmt);
    vfail(first.line, first.col, len > 0 ? len : 1, fmt, args);
    va_end(args);
}

/* "expected X" errors. When the wrong token is on a later line (or the program
 * has ended), what is missing belongs at the end of the previous token, so
 * that is where the error points. Otherwise it points at the wrong token. */
static void parse_error(const char *msg, Token got) {
    int after_prev = last_tok.line > 0 && (got.type == TOK_EOF || got.line > last_tok.line);
    char text[256];
    if (got.type == TOK_EOF)
        snprintf(text, sizeof text, "%s (reached the end of the program)", msg);
    else if (after_prev)
        snprintf(text, sizeof text, "%s (got '%s' on line %d)", msg, got.text, got.line);
    else
        snprintf(text, sizeof text, "%s (got '%s')", msg, got.text);

    if (after_prev) {
        /* build a fake one-character token just after the previous token */
        Token at = last_tok;
        at.col = last_tok.col + last_tok.len;
        at.len = 1;
        fail_tok(at, "%s", text);
    }
    fail_tok(got, "%s", text);
}

/* an error that always points at the token itself, however it is placed */
static void parse_error_at(const char *msg, Token got) {
    if (got.type == TOK_EOF) fail_tok(got, "%s (reached the end of the program)", msg);
    else                     fail_tok(got, "%s (got '%s')", msg, got.text);
}

/* Consumes the current token if it matches `type`, else reports an error.
 * A wrong token is left in place, so recovery can look at it.
 * `what` is the message, for example "expected ')' to close ...". */
static Token expect(TokenType type, const char *what) {
    Token t = lex_peek();
    if (t.type != type) parse_error(what, t);
    return advance();
}

/* ---- Forward declarations for the recursive-descent functions ----
 * The rules call each other in a circle (factor -> expr -> term -> unary ->
 * factor), so every function must be declared before any of them is defined. */
static ASTNode *parse_expr(void);      /* term (('+'|'-') term)* */
static ASTNode *parse_term(void);      /* unary ('*' unary)* */
static ASTNode *parse_unary(void);     /* '-' unary | NUMBER | factor */
static ASTNode *parse_factor(void);    /* ID | matrixLiteral | matrixGen | '(' expr ')' */
static ASTNode *parse_matrix_literal(void);
static ASTNode *parse_matrix_gen(void);
static ASTNode *parse_func_call(void);
static ASTNode *parse_statement(void);

/* Is this token the name of a function like add or inverse? Used to choose
 * between the funcCall rule and the expr rule on the right of '='. */
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
    Token first = lex_peek();
    expect(TOK_LBRACKET, "expected '[' to start matrix literal");

    double **rows = NULL;       /* array of rows, grown with realloc as rows arrive */
    int row_count = 0;
    int col_count = -1; /* unset until first row */

    do {
        Token row_start = lex_peek();
        expect(TOK_LBRACKET, "expected '[' to start a matrix row");

        double *row_values = NULL;
        int this_row_cols = 0;

        do {
            int negative = 0;
            if (lex_peek().type == TOK_MINUS) {
                advance();
                negative = 1;
            }
            Token num = expect(TOK_NUMBER, "expected a number inside matrix row");
            double value = atof(num.text);
            this_row_cols++;
            row_values = realloc(row_values, sizeof(double) * this_row_cols);
            /* "-0" stays 0, so it never prints as -0 */
            row_values[this_row_cols - 1] = (negative && value != 0) ? -value : value;
            /* keep going while a comma follows; "(advance(), 1)" eats the comma
             * and then gives true */
        } while (lex_peek().type == TOK_COMMA && (advance(), 1));

        expect(TOK_RBRACKET, "expected ']' to close matrix row");

        /* the first row fixes the width, every later row must match it */
        if (col_count == -1) {
            col_count = this_row_cols;
        } else if (this_row_cols != col_count) {
            fail_span(row_start, "matrix rows have inconsistent lengths (this row has %d, the first has %d)",
                      this_row_cols, col_count);
        }

        row_count++;
        rows = realloc(rows, sizeof(double *) * row_count);
        rows[row_count - 1] = row_values;

    } while (lex_peek().type == TOK_COMMA && (advance(), 1));

    expect(TOK_RBRACKET, "expected ']' to close matrix literal");

    /* underline the whole literal if it sits on one line, else just the first '[' */
    int len = (last_tok.line == first.line) ? last_tok.col + last_tok.len - first.col : first.len;
    return ast_at(ast_new_matrix_literal(rows, row_count, col_count, first.line), first.col, len);
}

/* One size inside identity(...) / zeros(...): a whole number from 1 up to
 * MAX_GEN_DIM. */
#define MAX_GEN_DIM 10000          /* biggest allowed rows or columns */
#define MAX_GEN_CELLS 10000000L    /* biggest allowed rows * columns, so a tiny program cannot ask for gigabytes */

/* Reads one NUMBER token and checks that it is a whole number in range.
 * `what` is the error message used if there is no number at all. */
static int parse_size(const char *what) {
    Token t = expect(TOK_NUMBER, what);
    if (strchr(t.text, '.')) {
        fail_tok(t, "a size must be a whole number (got '%s')", t.text);
    }
    long v = strtol(t.text, NULL, 10);
    if (v < 1 || v > MAX_GEN_DIM) {
        fail_tok(t, "a size must be between 1 and %d (got '%s')", MAX_GEN_DIM, t.text);
    }
    return (int)v;
}

/* matrixGen -> IDENTITY '(' NUMBER ')' | ZEROS '(' NUMBER ',' NUMBER ')' */
static ASTNode *parse_matrix_gen(void) {
    Token kw = advance();    /* the identity or zeros keyword */
    expect(TOK_LPAREN, "expected '(' after function name");

    int rows, cols;
    if (kw.type == TOK_IDENTITY) {
        rows = cols = parse_size("expected the size n in identity(n)");   /* square: one size */
    } else {
        rows = parse_size("expected the number of rows in zeros(rows, cols)");
        expect(TOK_COMMA, "expected ',' between the rows and columns in zeros(rows, cols)");
        cols = parse_size("expected the number of columns in zeros(rows, cols)");
    }
    expect(TOK_RPAREN, "expected ')' to close function call");

    /* cast to long first so rows * cols cannot overflow an int */
    if ((long)rows * cols > MAX_GEN_CELLS) {
        fail_span(kw, "a %dx%d matrix is too large (limit is %ld elements)", rows, cols, MAX_GEN_CELLS);
    }
    return ast_at(ast_new_matrix_gen(kw.type, rows, cols, kw.line), kw.col, kw.len);
}

/* factor -> ID | matrixLiteral | matrixGen | '(' expr ')'
 * A parenthesized expression is just its inner expression: the grouping
 * lives in the shape of the tree, so no extra node is needed. */
static ASTNode *parse_factor(void) {
    Token t = lex_peek();
    if (t.type == TOK_ID) {
        advance();
        return ast_at(ast_new_identifier(t.text, t.line), t.col, t.len);
    }
    if (t.type == TOK_LBRACKET) {
        return parse_matrix_literal();
    }
    if (t.type == TOK_IDENTITY || t.type == TOK_ZEROS) {
        return parse_matrix_gen();
    }
    if (t.type == TOK_LPAREN) {
        advance();
        /* every '(' goes one level deeper in the C call stack, so cap it */
        if (++nest_depth > MAX_NEST) {
            fail_tok(t, "expression is nested too deeply (limit is %d levels)", MAX_NEST);
        }
        ASTNode *inner = parse_expr();
        expect(TOK_RPAREN, "expected ')' to close the parenthesis");
        nest_depth--;
        return inner;
    }
    parse_error("expected a matrix name, a matrix, a number or '('", t);
    return NULL; /* unreachable, silences compiler warning */
}

/* -0 is just 0, so a scaled matrix never prints "-0" because of the number */
static double clean_zero(double v) {
    return v == 0 ? 0.0 : v;
}

/* factor * matrix: scaling a scaled matrix just multiplies the two numbers */
static ASTNode *make_scale(double factor, ASTNode *matrix, Token at) {
    if (matrix->type == NODE_SCALE) {
        /* 2 * (3 * A) becomes one node: Scale(6) over A */
        matrix->number = clean_zero(matrix->number * factor);
        return matrix;
    }
    return ast_at(ast_new_scale(clean_zero(factor), matrix, at.line), at.col, at.len);
}

/* '-' applied to a number gives a number, applied to a matrix it scales by -1 */
static ASTNode *negate(ASTNode *operand, Token minus) {
    if (operand->type == NODE_NUMBER) {
        operand->number = clean_zero(-operand->number);
        return operand;
    }
    return make_scale(-1.0, operand, minus);
}

/* one '*' step. Number * number stays a number, number * matrix (either
 * way round) becomes a scale, matrix * matrix is a real matrix multiply. */
static ASTNode *make_product(ASTNode *left, ASTNode *right, Token op) {
    if (left->type == NODE_NUMBER && right->type == NODE_NUMBER) {
        left->number = clean_zero(left->number * right->number);
        ast_free(right);
        return left;
    }
    if (left->type == NODE_NUMBER) {
        double k = left->number;
        ast_free(left);
        return make_scale(k, right, op);
    }
    if (right->type == NODE_NUMBER) {
        double k = right->number;
        ast_free(right);
        return make_scale(k, left, op);
    }
    return ast_at(ast_new_binop('*', left, right, op.line), op.col, op.len);
}

/* unary -> '-' unary | NUMBER | factor */
static ASTNode *parse_unary(void) {
    Token t = lex_peek();
    if (t.type == TOK_MINUS) {
        advance();
        if (++nest_depth > MAX_NEST) {
            fail_tok(t, "expression is nested too deeply (limit is %d levels)", MAX_NEST);
        }
        ASTNode *operand = parse_unary();   /* "- - A" is allowed, so recurse */
        nest_depth--;
        return negate(operand, t);
    }
    if (t.type == TOK_NUMBER) {
        advance();
        return ast_at(ast_new_number(atof(t.text), t.line), t.col, t.len);
    }
    return parse_factor();
}

/* term -> unary ('*' unary)*
 * A number on its own is not a matrix, so a term must not end up as one. */
static ASTNode *parse_term(void) {
    Token first = lex_peek();
    ASTNode *left = parse_unary();
    while (lex_peek().type == TOK_STAR) {
        Token op = advance();
        if (++stmt_ops > MAX_OPS) fail_tok(op, "expression is too long (limit is %d operators in one statement)", MAX_OPS);
        ASTNode *right = parse_unary();
        left = make_product(left, right, op);   /* folds into the left: A*B*C = (A*B)*C */
    }
    if (left->type == NODE_NUMBER) {
        fail_span(first, "a number must be multiplied with a matrix (for example 2 * A)");
    }
    return left;
}

/* expr -> term (('+'|'-') term)* */
static ASTNode *parse_expr(void) {
    ASTNode *left = parse_term();
    while (lex_peek().type == TOK_PLUS || lex_peek().type == TOK_MINUS) {
        Token op = advance();
        if (++stmt_ops > MAX_OPS) fail_tok(op, "expression is too long (limit is %d operators in one statement)", MAX_OPS);
        ASTNode *right = parse_term();
        char op_char = (op.type == TOK_PLUS) ? '+' : '-';
        /* the new node takes the old tree as its left child, so the tree leans left */
        left = ast_at(ast_new_binop(op_char, left, right, op.line), op.col, op.len);
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
    Token kw = advance(); /* the keyword token itself, e.g. TOK_ADD */
    expect(TOK_LPAREN, "expected '(' after function name");

    ASTNode **args = NULL;
    int arg_count = 0;

    do {
        Token id = expect(TOK_ID, "expected an identifier argument");
        arg_count++;
        args = realloc(args, sizeof(ASTNode *) * arg_count);
        args[arg_count - 1] = ast_at(ast_new_identifier(id.text, id.line), id.col, id.len);
    } while (lex_peek().type == TOK_COMMA && (advance(), 1));

    expect(TOK_RPAREN, "expected ')' to close function call");

    /* argument count rules, checked after the ')' so the whole call is underlined */
    if ((kw.type == TOK_TRANSPOSE || kw.type == TOK_INVERSE) && arg_count != 1) {
        fail_span(kw, "%s takes exactly 1 argument, got %d", kw.text, arg_count);
    }
    if ((kw.type == TOK_ADD || kw.type == TOK_SUBTRACT || kw.type == TOK_MULTIPLY) && arg_count < 2) {
        fail_span(kw, "%s needs at least 2 arguments, got %d", kw.text, arg_count);
    }

    return ast_at(ast_new_func_call(kw.type, args, arg_count, kw.line), kw.col, kw.len);
}

/* statement -> ID '=' (funcCall | expr)
 *            | PRINT '(' ID ')'
 *            | INPUT ID DIMS
 *            | OUTPUT ID
 * The first token tells which of the four it is (that is the one token of
 * lookahead the LL(1) grammar needs).
 */
static ASTNode *parse_statement(void) {
    Token t = lex_peek();

    if (t.type == TOK_PRINT) {
        advance();
        expect(TOK_LPAREN, "expected '(' after print");
        Token id = expect(TOK_ID, "expected an identifier inside print(...)");
        expect(TOK_RPAREN, "expected ')' to close print(...)");
        return ast_at(ast_new_print(id.text, t.line), id.col, id.len);
    }

    if (t.type == TOK_INPUT) {
        advance();
        Token id = expect(TOK_ID, "expected a matrix name after input");
        Token dims = expect(TOK_DIMS, "expected a size like 2x3 after the input name");

        /* the lexer guarantees the shape digits 'x' digits */
        char *end;
        long rows = strtol(dims.text, &end, 10);
        long cols = strtol(end + 1, NULL, 10);   /* end points at the 'x', so +1 skips it */
        /* the last test (cols > INT_MAX / rows) means rows * cols would overflow an int */
        if (rows < 1 || cols < 1 || rows > INT_MAX || cols > INT_MAX / rows) {
            fail_tok(dims, "invalid size '%s' for input '%s' "
                           "(rows and columns must be at least 1, and not too large)",
                     dims.text, id.text);
        }
        return ast_at(ast_new_input(id.text, (int)rows, (int)cols, t.line), id.col, id.len);
    }

    if (t.type == TOK_OUTPUT) {
        advance();
        Token id = expect(TOK_ID, "expected a matrix name after output");
        return ast_at(ast_new_output(id.text, t.line), id.col, id.len);
    }

    if (t.type == TOK_ID) {
        Token target = advance();
        expect(TOK_EQUALS, "expected '=' after identifier");

        /* a function keyword means funcCall, anything else is an infix expression */
        ASTNode *value = is_func_keyword(lex_peek().type)
                              ? parse_func_call()
                              : parse_expr();

        return ast_at(ast_new_assign(target.text, value, target.line), target.col, target.len);
    }

    /* a leftover '=' usually means the line above ended with an operator, so
     * the two lines got glued together */
    if (t.type == TOK_EQUALS) {
        fail_tok(t, "unexpected '=' (does the line above end with an operator?)");
    }
    parse_error_at("expected a statement (assignment, print, input or output)", t);
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
        advance();
    }
}

/* Prints every saved error, then a summary line, then stops the compiler
 * with exit code 1 (so scripts and the web server know it failed). */
static void report_errors_and_exit(void) {
    for (int i = 0; i < error_count; i++) {
        diag_print(stderr, "Parse", errors[i].line, errors[i].col, errors[i].len, errors[i].msg);
    }
    if (too_many_errors) {
        fprintf(stderr, "Too many errors, stopping after %d.\n", MAX_ERRORS);
    } else if (error_count > 1) {
        fprintf(stderr, "%d errors found.\n", error_count);
    }
    exit(1);
}

/* program -> statement* (until EOF)
 * The main loop. setjmp marks the spot that vfail() jumps back to. It returns
 * 0 the first time (normal flow) and 1 after a longjmp (an error happened). */
Program parse_program(const char *src) {
    lex_init(src);
    diag_set_source(src);
    error_count = 0;
    too_many_errors = 0;
    memset(&last_tok, 0, sizeof last_tok);

    Program prog = { NULL, 0 };
    while (lex_peek().type != TOK_EOF) {
        stmt_start_line = lex_peek().line;
        nest_depth = 0;     /* each statement starts with fresh limits */
        stmt_ops = 0;
        if (setjmp(stmt_jmp) == 0) {
            ASTNode *stmt = parse_statement();
            prog.count++;
            prog.statements = realloc(prog.statements, sizeof(ASTNode *) * prog.count);
            prog.statements[prog.count - 1] = stmt;
        } else {
            /* we got here by longjmp: the statement had an error */
            if (too_many_errors) break;
            skip_to_next_statement();
        }
    }
    if (error_count > 0) report_errors_and_exit();
    return prog;
}

/* Frees every statement tree and the array that held them. */
void program_free(Program *prog) {
    for (int i = 0; i < prog->count; i++) {
        ast_free(prog->statements[i]);
    }
    free(prog->statements);
    prog->statements = NULL;
    prog->count = 0;
}
