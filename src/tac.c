/*
 * tac.c : turns the checked AST into three-address code (see tac.h).
 *
 * The idea: walk each expression tree from the bottom up (post-order). Every
 * time a node needs a result, we make a new temporary name (t0, t1, ...) and
 * emit one instruction that fills it. gen_expr() returns the NAME of the place
 * holding the value of a subtree, so the parent can use that name as its own
 * operand.
 *
 *   C = (A + B) * D          tree:  *( +(A,B), D )
 *     gen_expr(+)  -> emits  t0 = A + B      returns "t0"
 *     gen_expr(D)  -> emits nothing           returns "D"
 *     gen_expr(*)  -> emits  t1 = t0 * D      returns "t1"
 *     the assignment then emits  C = t1
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"

/* ---- Growable instruction list ---- */
static TACProgram *g_tac;      /* the program we're building, set by tac_generate */
static int g_temp_counter;     /* next free temp number, t0, t1, ... */

/* Adds one blank instruction at the end of the list and returns a pointer to
 * it, for the caller to fill in. (The pointer is only good until the next
 * call, because realloc may move the array.) */
static TACInstr *tac_emit(void) {
    g_tac->count++;
    g_tac->instrs = realloc(g_tac->instrs, sizeof(TACInstr) * g_tac->count);
    TACInstr *instr = &g_tac->instrs[g_tac->count - 1];
    memset(instr, 0, sizeof(TACInstr));
    return instr;
}

/* Writes the next fresh temporary name ("t0", "t1", ...) into `out`. */
static void new_temp(char *out) {
    snprintf(out, TAC_NAME_MAX, "t%d", g_temp_counter++);
}

/* The text name of a function token, stored in the instruction. */
static const char *func_name_for(TokenType t) {
    switch (t) {
        case TOK_ADD:       return "add";
        case TOK_SUBTRACT:  return "subtract";
        case TOK_MULTIPLY:  return "multiply";
        case TOK_TRANSPOSE: return "transpose";
        case TOK_INVERSE:   return "inverse";
        case TOK_IDENTITY:  return "identity";
        case TOK_ZEROS:     return "zeros";
        default:            return "?";
    }
}

/* Generates code for an expression node and returns the "place" that
 * holds its value: either a variable name (for a bare identifier) or
 * a freshly emitted temp name. Caller owns `out` (size TAC_NAME_MAX).
 */
static void gen_expr(ASTNode *node, char *out) {
    switch (node->type) {

        case NODE_IDENTIFIER:
            /* Already a named value, no instruction needed. */
            strncpy(out, node->name, TAC_NAME_MAX - 1);
            break;

        case NODE_MATRIX_LITERAL: {
            char t[TAC_NAME_MAX];
            new_temp(t);
            TACInstr *ins = tac_emit();
            ins->op = TAC_LOAD_MATRIX;
            ins->line = node->line;
            strncpy(ins->result, t, TAC_NAME_MAX - 1);
            ins->rows = node->rows;
            ins->cols = node->cols;
            /* Deep copy the numbers so TAC has its own lifetime, independent
             * of the AST (which may be freed before we execute this TAC).
             */
            ins->matrix_values = malloc(sizeof(double *) * node->rows);
            for (int r = 0; r < node->rows; r++) {
                ins->matrix_values[r] = malloc(sizeof(double) * node->cols);
                memcpy(ins->matrix_values[r], node->values[r], sizeof(double) * node->cols);
            }
            strncpy(out, t, TAC_NAME_MAX - 1);
            break;
        }

        case NODE_MATRIX_GEN: {
            char t[TAC_NAME_MAX];
            new_temp(t);
            TACInstr *ins = tac_emit();
            ins->op = TAC_GEN;
            ins->line = node->line;
            strncpy(ins->result, t, TAC_NAME_MAX - 1);
            strncpy(ins->func_name, func_name_for(node->func_kind), sizeof(ins->func_name) - 1);
            ins->rows = node->rows;
            ins->cols = node->cols;
            strncpy(out, t, TAC_NAME_MAX - 1);
            break;
        }

        case NODE_SCALE: {
            char m[TAC_NAME_MAX], t[TAC_NAME_MAX];
            gen_expr(node->left, m);   /* the scaled matrix first (children before parent) */
            new_temp(t);
            TACInstr *ins = tac_emit();
            ins->op = TAC_SCALE;
            ins->line = node->line;
            strncpy(ins->result, t, TAC_NAME_MAX - 1);
            strncpy(ins->arg1, m, TAC_NAME_MAX - 1);
            ins->scalar = node->number;
            strncpy(out, t, TAC_NAME_MAX - 1);
            break;
        }

        case NODE_BINOP: {
            char l[TAC_NAME_MAX], r[TAC_NAME_MAX], t[TAC_NAME_MAX];
            gen_expr(node->left, l);    /* left subtree first, then right, then this node */
            gen_expr(node->right, r);
            new_temp(t);
            TACInstr *ins = tac_emit();
            ins->op = TAC_BINOP;
            ins->line = node->line;
            strncpy(ins->result, t, TAC_NAME_MAX - 1);
            strncpy(ins->arg1, l, TAC_NAME_MAX - 1);
            strncpy(ins->arg2, r, TAC_NAME_MAX - 1);
            ins->binop = node->op;
            strncpy(out, t, TAC_NAME_MAX - 1);
            break;
        }

        case NODE_FUNC_CALL: {
            char t[TAC_NAME_MAX];
            new_temp(t);
            TACInstr *ins = tac_emit();
            ins->op = TAC_CALL;
            ins->line = node->line;
            strncpy(ins->result, t, TAC_NAME_MAX - 1);
            strncpy(ins->func_name, func_name_for(node->func_kind), sizeof(ins->func_name) - 1);
            ins->call_arg_count = node->arg_count;
            for (int i = 0; i < node->arg_count && i < TAC_MAX_CALL_ARGS; i++) {
                /* Grammar guarantees every arg is a plain identifier. */
                strncpy(ins->call_args[i], node->args[i]->name, TAC_NAME_MAX - 1);
            }
            strncpy(out, t, TAC_NAME_MAX - 1);
            break;
        }

        default:
            fprintf(stderr, "internal error: unexpected node kind in gen_expr\n");
            exit(1);
    }
}

/* Generates code for one top-level statement (assign, print, input, output). */
static void gen_statement(ASTNode *stmt) {
    if (stmt->type == NODE_ASSIGN) {
        char place[TAC_NAME_MAX];
        gen_expr(stmt->value, place);

        /* If the expr already computed straight into a temp, alias that
         * temp to the target name with one copy instruction. If the RHS
         * was a bare identifier (X = Y), this also correctly emits
         * "X = Y" rather than silently dropping the assignment.
         */
        TACInstr *ins = tac_emit();
        ins->op = TAC_COPY;
        ins->line = stmt->line;
        strncpy(ins->result, stmt->target, TAC_NAME_MAX - 1);
        strncpy(ins->src, place, TAC_NAME_MAX - 1);
        /* a copy from a fresh temp can later MOVE the data instead of copying
         * it, since nothing else reads that temp. A copy from a real
         * variable must keep both. */
        ins->src_is_temp = (stmt->value->type != NODE_IDENTIFIER);

    } else if (stmt->type == NODE_PRINT) {
        TACInstr *ins = tac_emit();
        ins->op = TAC_PRINT;
        ins->line = stmt->line;
        strncpy(ins->src, stmt->print_target, TAC_NAME_MAX - 1);

    } else if (stmt->type == NODE_INPUT) {
        TACInstr *ins = tac_emit();
        ins->op = TAC_INPUT;
        ins->line = stmt->line;
        strncpy(ins->result, stmt->name, TAC_NAME_MAX - 1);
        ins->rows = stmt->rows;
        ins->cols = stmt->cols;

    } else if (stmt->type == NODE_OUTPUT) {
        TACInstr *ins = tac_emit();
        ins->op = TAC_OUTPUT;
        ins->line = stmt->line;
        strncpy(ins->src, stmt->name, TAC_NAME_MAX - 1);
        ins->rows = stmt->result_rows;   /* filled in earlier by the semantic check */
        ins->cols = stmt->result_cols;

    } else {
        fprintf(stderr, "internal error: unexpected node kind at statement level\n");
        exit(1);
    }
}

/* The entry point (see tac.h): one pass over the statements, in order. */
TACProgram tac_generate(Program *prog) {
    TACProgram tac = { NULL, 0 };
    g_tac = &tac;
    g_temp_counter = 0;

    for (int i = 0; i < prog->count; i++) {
        gen_statement(prog->statements[i]);
    }

    g_tac = NULL;   /* tac is a local variable: do not leave a dangling pointer */
    return tac;
}

/* Prints the instruction list as readable text, one line per instruction.
 * %-4s pads the result name to 4 characters so the "=" signs line up. */
void tac_print(const TACProgram *tac) {
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        switch (ins->op) {
            case TAC_LOAD_MATRIX:
                printf("%-4s = LOADMATRIX %dx%d\n", ins->result, ins->rows, ins->cols);
                break;
            case TAC_GEN:
                if (strcmp(ins->func_name, "identity") == 0)
                    printf("%-4s = identity(%d)\n", ins->result, ins->rows);
                else
                    printf("%-4s = zeros(%d, %d)\n", ins->result, ins->rows, ins->cols);
                break;
            case TAC_SCALE:
                printf("%-4s = %.10g * %s\n", ins->result, ins->scalar, ins->arg1);
                break;
            case TAC_BINOP:
                printf("%-4s = %s %c %s\n", ins->result, ins->arg1, ins->binop, ins->arg2);
                break;
            case TAC_CALL:
                printf("%-4s = %s(", ins->result, ins->func_name);
                for (int a = 0; a < ins->call_arg_count; a++) {
                    printf("%s%s", ins->call_args[a], a + 1 < ins->call_arg_count ? ", " : "");
                }
                printf(")\n");
                break;
            case TAC_COPY:
                printf("%-4s = %s\n", ins->result, ins->src);
                break;
            case TAC_PRINT:
                printf("PRINT %s\n", ins->src);
                break;
            case TAC_INPUT:
                printf("INPUT %s %dx%d\n", ins->result, ins->rows, ins->cols);
                break;
            case TAC_OUTPUT:
                printf("OUTPUT %s\n", ins->src);
                break;
        }
    }
}

/* Frees the list. Only LOADMATRIX instructions own extra memory (the copy of
 * the literal's numbers), so those are freed row by row first. */
void tac_free(TACProgram *tac) {
    for (int i = 0; i < tac->count; i++) {
        if (tac->instrs[i].op == TAC_LOAD_MATRIX) {
            for (int r = 0; r < tac->instrs[i].rows; r++) {
                free(tac->instrs[i].matrix_values[r]);
            }
            free(tac->instrs[i].matrix_values);
        }
    }
    free(tac->instrs);
    tac->instrs = NULL;
    tac->count = 0;
}
