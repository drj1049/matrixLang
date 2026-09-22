#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"

/* ---- Growable instruction list ---- */
static TACProgram *g_tac;      /* the program we're building, set by tac_generate */
static int g_temp_counter;     /* next free temp number, t0, t1, ... */

static TACInstr *tac_emit(void) {
    g_tac->count++;
    g_tac->instrs = realloc(g_tac->instrs, sizeof(TACInstr) * g_tac->count);
    TACInstr *instr = &g_tac->instrs[g_tac->count - 1];
    memset(instr, 0, sizeof(TACInstr));
    return instr;
}

static void new_temp(char *out) {
    snprintf(out, TAC_NAME_MAX, "t%d", g_temp_counter++);
}

static const char *func_name_for(TokenType t) {
    switch (t) {
        case TOK_ADD:       return "add";
        case TOK_SUBTRACT:  return "subtract";
        case TOK_MULTIPLY:  return "multiply";
        case TOK_TRANSPOSE: return "transpose";
        case TOK_INVERSE:   return "inverse";
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

        case NODE_BINOP: {
            char l[TAC_NAME_MAX], r[TAC_NAME_MAX], t[TAC_NAME_MAX];
            gen_expr(node->left, l);
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

    } else if (stmt->type == NODE_PRINT) {
        TACInstr *ins = tac_emit();
        ins->op = TAC_PRINT;
        ins->line = stmt->line;
        strncpy(ins->src, stmt->print_target, TAC_NAME_MAX - 1);

    } else {
        fprintf(stderr, "internal error: unexpected node kind at statement level\n");
        exit(1);
    }
}

TACProgram tac_generate(Program *prog) {
    TACProgram tac = { NULL, 0 };
    g_tac = &tac;
    g_temp_counter = 0;

    for (int i = 0; i < prog->count; i++) {
        gen_statement(prog->statements[i]);
    }

    g_tac = NULL;
    return tac;
}

void tac_print(const TACProgram *tac) {
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        switch (ins->op) {
            case TAC_LOAD_MATRIX:
                printf("%-4s = LOADMATRIX %dx%d\n", ins->result, ins->rows, ins->cols);
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
        }
    }
}

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