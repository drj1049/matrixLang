#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "codegen.h"

static const char *binop_fn(char op) {
    switch (op) {
        case '+': return "mat_add";
        case '-': return "mat_sub";
        default:  return "mat_mul";
    }
}

static const char *call_fn(const char *name) {
    if (strcmp(name, "add") == 0)       return "mat_add";
    if (strcmp(name, "subtract") == 0)  return "mat_sub";
    if (strcmp(name, "multiply") == 0)  return "mat_mul";
    if (strcmp(name, "transpose") == 0) return "mat_transpose";
    return "mat_inverse";
}

int codegen_is_function(const TACProgram *tac) {
    for (int i = 0; i < tac->count; i++) {
        if (tac->instrs[i].op == TAC_INPUT || tac->instrs[i].op == TAC_OUTPUT) return 1;
    }
    return 0;
}

static int is_input(const TACProgram *tac, const char *name) {
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        if (ins->op == TAC_INPUT && strcmp(ins->result, name) == 0) return 1;
    }
    return 0;
}

static const TACInstr *find_output(const TACProgram *tac) {
    for (int i = 0; i < tac->count; i++) {
        if (tac->instrs[i].op == TAC_OUTPUT) return &tac->instrs[i];
    }
    return NULL;
}

/* every variable and temp the generated code owns, each listed once
 * (inputs are not in here: they belong to the caller) */
static char (*collect_locals(const TACProgram *tac, int *count))[TAC_NAME_MAX] {
    char (*names)[TAC_NAME_MAX] = malloc((size_t)(tac->count + 1) * TAC_NAME_MAX);
    int n = 0;
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        if (ins->op == TAC_PRINT || ins->op == TAC_INPUT || ins->op == TAC_OUTPUT) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) {
            if (strcmp(names[j], ins->result) == 0) { dup = 1; break; }
        }
        if (!dup) strcpy(names[n++], ins->result);
    }
    *count = n;
    return names;
}

/* "Mat name(Mat v_A, Mat v_B)" for the definition; the header uses the plain
 * input names ("Mat A, Mat B") since the v_ prefix is an internal detail */
static void emit_signature(const TACProgram *tac, const char *func_name, int internal_names, FILE *out) {
    int n = 0;
    fprintf(out, "Mat %s(", func_name);
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        if (ins->op != TAC_INPUT) continue;
        fprintf(out, "%sMat %s%s", n++ ? ", " : "", internal_names ? "v_" : "", ins->result);
    }
    if (n == 0) fprintf(out, "void");
    fprintf(out, ")");
}

/* Every local owns its data: results are stored with mat_set, which frees
 * the previous value, so nothing leaks even when a name is reassigned. */
static void emit_body(const TACProgram *tac, FILE *out) {
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        switch (ins->op) {
        case TAC_LOAD_MATRIX:
            fprintf(out, "    mat_set(&v_%s, mat_from(%d, %d, (double[]){",
                    ins->result, ins->rows, ins->cols);
            for (int r = 0; r < ins->rows; r++) {
                for (int c = 0; c < ins->cols; c++) {
                    if (r > 0 || c > 0) fprintf(out, ", ");
                    fprintf(out, "%.17g", ins->matrix_values[r][c]);
                }
            }
            fprintf(out, "}));\n");
            break;

        case TAC_GEN:
            if (strcmp(ins->func_name, "identity") == 0)
                fprintf(out, "    mat_set(&v_%s, mat_identity(%d));\n", ins->result, ins->rows);
            else
                fprintf(out, "    mat_set(&v_%s, mat_new(%d, %d));\n", ins->result, ins->rows, ins->cols);
            break;

        case TAC_BINOP:
            fprintf(out, "    mat_set(&v_%s, %s(v_%s, v_%s));\n",
                    ins->result, binop_fn(ins->binop), ins->arg1, ins->arg2);
            break;

        case TAC_CALL: {
            const char *fn = call_fn(ins->func_name);
            if (strcmp(ins->func_name, "transpose") == 0 ||
                strcmp(ins->func_name, "inverse") == 0) {
                fprintf(out, "    mat_set(&v_%s, %s(v_%s));\n",
                        ins->result, fn, ins->call_args[0]);
                break;
            }
            /* fold left to right, one step per line so each partial
             * result is freed as soon as the next one replaces it */
            fprintf(out, "    mat_set(&v_%s, %s(v_%s, v_%s));\n",
                    ins->result, fn, ins->call_args[0], ins->call_args[1]);
            for (int k = 2; k < ins->call_arg_count; k++) {
                fprintf(out, "    mat_set(&v_%s, %s(v_%s, v_%s));\n",
                        ins->result, fn, ins->result, ins->call_args[k]);
            }
            break;
        }

        case TAC_COPY:
            if (ins->src_is_temp) {
                fprintf(out, "    mat_move(&v_%s, &v_%s);\n", ins->result, ins->src);
            } else {
                /* two names must never share one buffer */
                fprintf(out, "    mat_set(&v_%s, mat_copy(v_%s));\n", ins->result, ins->src);
            }
            break;

        case TAC_PRINT:
            fprintf(out, "    mat_print(\"%s\", v_%s);\n", ins->src, ins->src);
            break;

        case TAC_INPUT:   /* checked at the top of the function */
        case TAC_OUTPUT:  /* returned at the bottom */
            break;
        }
    }
}

void codegen_c(const TACProgram *tac, const char *func_name, FILE *out) {
    int as_function = codegen_is_function(tac);
    const TACInstr *output = find_output(tac);
    int nlocals;
    char (*locals)[TAC_NAME_MAX] = collect_locals(tac, &nlocals);

    fprintf(out, "/* generated by the MatrixLang compiler */\n");
    fprintf(out, "#include \"matrt.h\"\n\n");

    if (as_function) {
        emit_signature(tac, func_name, 1, out);
        fprintf(out, " {\n");
        for (int i = 0; i < tac->count; i++) {
            const TACInstr *ins = &tac->instrs[i];
            if (ins->op != TAC_INPUT) continue;
            fprintf(out, "    mat_check(\"%s\", \"%s\", v_%s, %d, %d);\n",
                    func_name, ins->result, ins->result, ins->rows, ins->cols);
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "int main(void) {\n");
    }

    if (nlocals > 0) {
        for (int i = 0; i < nlocals; i++) fprintf(out, "    Mat v_%s = {0};\n", locals[i]);
        fprintf(out, "\n");
        emit_body(tac, out);
        fprintf(out, "\n");
    }

    /* free everything this code allocated, except the matrix being returned */
    int returns_input = output && is_input(tac, output->src);
    if (returns_input) {
        /* never hand the caller's own data back: they would free it twice */
        fprintf(out, "    Mat result = mat_copy(v_%s);\n", output->src);
    }
    for (int i = 0; i < nlocals; i++) {
        if (output && !returns_input && strcmp(locals[i], output->src) == 0) continue;
        fprintf(out, "    mat_free(v_%s);\n", locals[i]);
    }

    if (!as_function)       fprintf(out, "    return 0;\n}\n");
    else if (returns_input) fprintf(out, "    return result;\n}\n");
    else if (output)        fprintf(out, "    return v_%s;\n}\n", output->src);

    free(locals);
}

void codegen_header(const TACProgram *tac, const char *func_name, FILE *out) {
    const TACInstr *output = find_output(tac);
    int ninputs = 0;

    fprintf(out, "/* generated by the MatrixLang compiler */\n");
    fprintf(out, "#ifndef ");
    for (const char *p = func_name; *p; p++) fputc(toupper((unsigned char)*p), out);
    fprintf(out, "_H\n#define ");
    for (const char *p = func_name; *p; p++) fputc(toupper((unsigned char)*p), out);
    fprintf(out, "_H\n\n#include \"matrt.h\"\n\n");

    fprintf(out, "/* Inputs, each a Mat {rows, cols, data} with data in row-major order:\n");
    for (int i = 0; i < tac->count; i++) {
        const TACInstr *ins = &tac->instrs[i];
        if (ins->op != TAC_INPUT) continue;
        fprintf(out, " *   %s: %dx%d\n", ins->result, ins->rows, ins->cols);
        ninputs++;
    }
    if (ninputs == 0) fprintf(out, " *   (none)\n");
    fprintf(out, " * Inputs are only read, never modified or freed.\n");
    fprintf(out, " * An input of the wrong size prints an error and exits, and so does\n");
    fprintf(out, " * inverse() of a singular matrix.\n");
    fprintf(out, " * Returns %s as a new %dx%d matrix; the caller frees it with mat_free().\n",
            output->src, output->rows, output->cols);
    fprintf(out, " */\n");
    fprintf(out, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    emit_signature(tac, func_name, 0, out);
    fprintf(out, ";\n\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
}
