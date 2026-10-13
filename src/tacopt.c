#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tacopt.h"

/* ---- Value numbers ----
 * A VNode describes one distinct value the program can hold. Two variables
 * with the same value number hold the same matrix. The operands of a node
 * are themselves value numbers, so equal expressions get equal numbers
 * however they were written (A + B and B + A, add(A,B) and A + B, ...).
 */
typedef enum {
    VN_INPUT,       /* a matrix passed in by the caller: every one is its own value */
    VN_LOAD,        /* a matrix literal; equal literals are the same value */
    VN_IDENTITY,
    VN_ZEROS,
    VN_SCALE,       /* k * a */
    VN_BIN,         /* a op b, op is '+', '-' or '*' */
    VN_TRANSPOSE,
    VN_INVERSE
} VNKind;

typedef struct {
    VNKind kind;
    int rows, cols;
    int a, b;           /* operand value numbers */
    char op;
    double k;
    double **lit;       /* VN_LOAD: the numbers (owned by the TAC instruction) */
    int id;             /* VN_INPUT: a number of its own, so no two inputs are equal */
    int line;           /* source line where the value was first computed */
} VNode;

/* A name and the value number of what it holds right now. */
typedef struct {
    char name[TAC_NAME_MAX];
    int vn;
    int consumed;       /* a temporary whose data was moved into a variable: gone */
} Var;

static VNode *vns;
static int vn_count, vn_cap;
static Var *vars;
static int var_count, var_cap;
static int next_input_id;
static int simplified;  /* set when an algebra rule fired for the current instruction */

static int is_temp_name(const char *name) {
    if (name[0] != 't' || name[1] == '\0') return 0;
    for (const char *p = name + 1; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

/* ---- tables ---- */
static int vn_equal(const VNode *x, const VNode *y) {
    if (x->kind != y->kind || x->rows != y->rows || x->cols != y->cols) return 0;
    switch (x->kind) {
        case VN_INPUT:    return x->id == y->id;
        case VN_LOAD:
            for (int r = 0; r < x->rows; r++) {
                if (memcmp(x->lit[r], y->lit[r], sizeof(double) * (size_t)x->cols) != 0) return 0;
            }
            return 1;
        case VN_IDENTITY:
        case VN_ZEROS:    return 1;
        case VN_SCALE:    return x->a == y->a && x->k == y->k;
        case VN_BIN:      return x->op == y->op && x->a == y->a && x->b == y->b;
        case VN_TRANSPOSE:
        case VN_INVERSE:  return x->a == y->a;
    }
    return 0;
}

/* the number of an equal node if there is one, else a new number */
static int intern(VNode n) {
    for (int i = 0; i < vn_count; i++) {
        if (vn_equal(&vns[i], &n)) return i;
    }
    if (vn_count == vn_cap) {
        vn_cap = vn_cap ? vn_cap * 2 : 32;
        vns = realloc(vns, sizeof(VNode) * (size_t)vn_cap);
    }
    vns[vn_count] = n;
    return vn_count++;
}

static int var_index(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return i;
    }
    return -1;
}

static void var_set(const char *name, int vn) {
    int i = var_index(name);
    if (i == -1) {
        if (var_count == var_cap) {
            var_cap = var_cap ? var_cap * 2 : 32;
            vars = realloc(vars, sizeof(Var) * (size_t)var_cap);
        }
        i = var_count++;
        memset(&vars[i], 0, sizeof(Var));
        strncpy(vars[i].name, name, TAC_NAME_MAX - 1);
    }
    vars[i].vn = vn;
    vars[i].consumed = 0;
}

static int var_vn(const char *name) {
    int i = var_index(name);
    if (i != -1) return vars[i].vn;
    /* never defined (should not happen after semantic analysis): treat as an unknown value */
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_INPUT;
    n.id = next_input_id++;
    int v = intern(n);
    var_set(name, v);
    return v;
}

/* A variable that still holds this value. Names the user wrote come first. */
static const Var *find_holder(int vn, int user_only) {
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < var_count; i++) {
            if (vars[i].vn != vn || vars[i].consumed) continue;
            if (pass == 0 && is_temp_name(vars[i].name)) continue;
            if (pass == 1 && (user_only || !is_temp_name(vars[i].name))) continue;
            return &vars[i];
        }
    }
    return NULL;
}

/* ---- building value numbers, with the algebra rules ---- */
static int vn_zeros(int rows, int cols, int line) {
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_ZEROS; n.rows = rows; n.cols = cols; n.line = line;
    return intern(n);
}

static int vn_scale(double k, int a, int line) {
    VNode A = vns[a];
    if (k == 1.0) { simplified = 1; return a; }
    if (k == 0.0 || A.kind == VN_ZEROS) { simplified = 1; return vn_zeros(A.rows, A.cols, line); }
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_SCALE; n.rows = A.rows; n.cols = A.cols; n.a = a; n.k = k; n.line = line;
    return intern(n);
}

static int vn_bin(char op, int a, int b, int line) {
    VNode A = vns[a], B = vns[b];
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_BIN; n.op = op; n.line = line;

    if (op == '*') {
        if (A.kind == VN_IDENTITY) { simplified = 1; return b; }
        if (B.kind == VN_IDENTITY) { simplified = 1; return a; }
        if (A.kind == VN_ZEROS || B.kind == VN_ZEROS) {
            simplified = 1;
            return vn_zeros(A.rows, B.cols, line);
        }
        n.rows = A.rows; n.cols = B.cols;
    } else if (op == '+') {
        if (A.kind == VN_ZEROS) { simplified = 1; return b; }
        if (B.kind == VN_ZEROS) { simplified = 1; return a; }
        n.rows = A.rows; n.cols = A.cols;
        if (a > b) { int t = a; a = b; b = t; }      /* A + B is B + A */
    } else {
        if (B.kind == VN_ZEROS) { simplified = 1; return a; }
        n.rows = A.rows; n.cols = A.cols;
    }
    n.a = a; n.b = b;
    return intern(n);
}

static int vn_transpose(int a, int line) {
    VNode A = vns[a];
    if (A.kind == VN_TRANSPOSE) { simplified = 1; return A.a; }
    if (A.kind == VN_IDENTITY)  { simplified = 1; return a; }
    if (A.kind == VN_ZEROS)     { simplified = 1; return vn_zeros(A.cols, A.rows, line); }
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_TRANSPOSE; n.rows = A.cols; n.cols = A.rows; n.a = a; n.line = line;
    return intern(n);
}

static int vn_inverse(int a, int line) {
    VNode A = vns[a];
    if (A.kind == VN_INVERSE)  { simplified = 1; return A.a; }
    if (A.kind == VN_IDENTITY) { simplified = 1; return a; }
    VNode n;
    memset(&n, 0, sizeof n);
    n.kind = VN_INVERSE; n.rows = A.rows; n.cols = A.cols; n.a = a; n.line = line;
    return intern(n);
}

/* ---- text for the notes ---- */
static void app(char *buf, size_t cap, const char *s) {
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    strncat(buf, s, cap - len - 1);
}

static void vn_text(int v, char *buf, size_t cap, int depth);

static void operand_text(int v, char *buf, size_t cap, int depth) {
    /* keep nested expressions readable: (A * B) */
    int nested = vns[v].kind == VN_BIN || vns[v].kind == VN_SCALE;
    const Var *h = find_holder(v, 1);
    if (nested && !h) app(buf, cap, "(");
    vn_text(v, buf, cap, depth);
    if (nested && !h) app(buf, cap, ")");
}

static void vn_text(int v, char *buf, size_t cap, int depth) {
    const Var *h = find_holder(v, 1);
    if (h) { app(buf, cap, h->name); return; }
    if (depth > 4) { app(buf, cap, "..."); return; }

    VNode n = vns[v];
    char tmp[64];
    switch (n.kind) {
        case VN_INPUT:    app(buf, cap, "an input"); break;
        case VN_LOAD:     app(buf, cap, "a matrix literal"); break;
        case VN_IDENTITY: snprintf(tmp, sizeof tmp, "identity(%d)", n.rows); app(buf, cap, tmp); break;
        case VN_ZEROS:    snprintf(tmp, sizeof tmp, "zeros(%d, %d)", n.rows, n.cols); app(buf, cap, tmp); break;
        case VN_SCALE:
            snprintf(tmp, sizeof tmp, "%.10g * ", n.k);
            app(buf, cap, tmp);
            operand_text(n.a, buf, cap, depth + 1);
            break;
        case VN_BIN:
            operand_text(n.a, buf, cap, depth + 1);
            snprintf(tmp, sizeof tmp, " %c ", n.op);
            app(buf, cap, tmp);
            operand_text(n.b, buf, cap, depth + 1);
            break;
        case VN_TRANSPOSE:
        case VN_INVERSE:
            app(buf, cap, n.kind == VN_TRANSPOSE ? "transpose(" : "inverse(");
            vn_text(n.a, buf, cap, depth + 1);
            app(buf, cap, ")");
            break;
    }
}

/* an operand as the instruction names it: a variable you wrote, or what a temporary holds */
static void name_text(const char *name, char *buf, size_t cap) {
    if (!is_temp_name(name)) { app(buf, cap, name); return; }
    operand_text(var_vn(name), buf, cap, 0);
}

static void instr_text(const TACInstr *ins, char *buf, size_t cap) {
    char tmp[64];
    buf[0] = '\0';
    if (ins->op == TAC_SCALE) {
        snprintf(tmp, sizeof tmp, "%.10g * ", ins->scalar);
        app(buf, cap, tmp);
        name_text(ins->arg1, buf, cap);
    } else if (ins->op == TAC_BINOP) {
        name_text(ins->arg1, buf, cap);
        snprintf(tmp, sizeof tmp, " %c ", ins->binop);
        app(buf, cap, tmp);
        name_text(ins->arg2, buf, cap);
    } else {
        app(buf, cap, ins->func_name);
        app(buf, cap, "(");
        for (int i = 0; i < ins->call_arg_count; i++) {
            if (i) app(buf, cap, ", ");
            name_text(ins->call_args[i], buf, cap);
        }
        app(buf, cap, ")");
    }
}

/* ---- the pass ---- */
static int compute(const TACInstr *ins) {
    if (ins->op == TAC_SCALE) {
        return vn_scale(ins->scalar, var_vn(ins->arg1), ins->line);
    }
    if (ins->op == TAC_BINOP) {
        int a = var_vn(ins->arg1);
        int b = var_vn(ins->arg2);
        return vn_bin(ins->binop, a, b, ins->line);
    }
    /* TAC_CALL */
    int acc = var_vn(ins->call_args[0]);
    if (strcmp(ins->func_name, "transpose") == 0) return vn_transpose(acc, ins->line);
    if (strcmp(ins->func_name, "inverse") == 0)   return vn_inverse(acc, ins->line);
    char op = strcmp(ins->func_name, "add") == 0 ? '+'
            : strcmp(ins->func_name, "subtract") == 0 ? '-' : '*';
    /* add(a, b, c) is (a + b) + c, the same order the generated code uses */
    for (int i = 1; i < ins->call_arg_count; i++) {
        acc = vn_bin(op, acc, var_vn(ins->call_args[i]), ins->line);
    }
    return acc;
}

static void clear_operands(TACInstr *ins) {
    ins->arg1[0] = ins->arg2[0] = '\0';
    ins->binop = 0;
    ins->scalar = 0;
    ins->func_name[0] = '\0';
    ins->call_arg_count = 0;
    ins->src[0] = '\0';
    ins->src_is_temp = 0;
}

static void note(FILE *notes, const char *kind, int line, const char *text) {
    if (notes) fprintf(notes, "%s\t%d\t%s\n", kind, line, text);
}

/* Replace ins by something cheaper if its value is already known. Returns 1 if changed. */
static int rewrite(TACInstr *ins, int v, int before, const char *what, FILE *notes) {
    VNode n = vns[v];
    char text[400];

    if (n.kind == VN_IDENTITY || n.kind == VN_ZEROS) {
        /* build it directly instead of computing it */
        clear_operands(ins);
        ins->op = TAC_GEN;
        strcpy(ins->func_name, n.kind == VN_IDENTITY ? "identity" : "zeros");
        ins->rows = n.rows;
        ins->cols = n.cols;
        if (n.kind == VN_IDENTITY) snprintf(text, sizeof text, "%s simplifies to identity(%d)", what, n.rows);
        else snprintf(text, sizeof text, "%s simplifies to zeros(%d, %d)", what, n.rows, n.cols);
        note(notes, "REWRITE", ins->line, text);
        return 1;
    }

    if (v >= before) return 0;                 /* a value computed here for the first time */

    const Var *h = find_holder(v, 0);
    if (!h) return 0;                          /* known, but no variable still holds it */

    char holder[TAC_NAME_MAX];
    strcpy(holder, h->name);
    if (simplified) {
        snprintf(text, sizeof text, "%s simplifies to ", what);
        vn_text(v, text, sizeof text, 0);
        note(notes, "REWRITE", ins->line, text);
    } else {
        snprintf(text, sizeof text, "%s was already computed on line %d (it is in %s), so it is reused",
                 what, vns[v].line, is_temp_name(holder) ? "an earlier result" : holder);
        note(notes, "REUSE", ins->line, text);
    }
    clear_operands(ins);
    ins->op = TAC_COPY;
    strncpy(ins->src, holder, TAC_NAME_MAX - 1);
    return 1;
}

int tac_optimize(TACProgram *tac, FILE *notes) {
    int changes = 0;
    vns = NULL; vn_count = vn_cap = 0;
    vars = NULL; var_count = var_cap = 0;
    next_input_id = 0;

    for (int i = 0; i < tac->count; i++) {
        TACInstr *ins = &tac->instrs[i];
        VNode n;
        memset(&n, 0, sizeof n);
        n.line = ins->line;

        switch (ins->op) {
        case TAC_INPUT:
            n.kind = VN_INPUT; n.rows = ins->rows; n.cols = ins->cols; n.id = next_input_id++;
            var_set(ins->result, intern(n));
            break;

        case TAC_LOAD_MATRIX:
            n.kind = VN_LOAD; n.rows = ins->rows; n.cols = ins->cols; n.lit = ins->matrix_values;
            var_set(ins->result, intern(n));
            break;

        case TAC_GEN:
            n.kind = strcmp(ins->func_name, "identity") == 0 ? VN_IDENTITY : VN_ZEROS;
            n.rows = ins->rows; n.cols = ins->cols;
            var_set(ins->result, intern(n));
            break;

        case TAC_COPY: {
            int v = var_vn(ins->src);
            if (ins->src_is_temp) {
                /* the temporary's data moves into the variable, so it holds nothing afterwards */
                int t = var_index(ins->src);
                if (t != -1) vars[t].consumed = 1;
            }
            var_set(ins->result, v);
            break;
        }

        case TAC_SCALE:
        case TAC_BINOP:
        case TAC_CALL: {
            int before = vn_count;
            char what[200];
            simplified = 0;
            instr_text(ins, what, sizeof what);       /* before the instruction changes */
            int v = compute(ins);
            changes += rewrite(ins, v, before, what, notes);
            var_set(ins->result, v);
            break;
        }

        case TAC_PRINT:
        case TAC_OUTPUT:
            break;
        }
    }

    free(vns);
    free(vars);
    vns = NULL; vars = NULL;
    return changes;
}
