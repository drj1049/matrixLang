#!/usr/bin/env python3
"""Phase 3 test script for MatrixLang: 12 test cases, two for each kind of input (valid, invalid,
semantic, boundary, run-time and complex). Expected answers are worked out here with
numpy / plain Python, never taken from the compiler's own output.

usage (from the project folder, after `make`):
    python3 tests/phase3_tests.py            # prints the failures and a summary line
    python3 tests/phase3_tests.py out.json   # also writes every case to a JSON file
Needs numpy (pip install numpy)."""
import json, os, shutil, subprocess, sys, tempfile
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMP, INTERP, SRC = ROOT + "/build/compiler", ROOT + "/build/interp_test", ROOT + "/src"
TMP = tempfile.mkdtemp(prefix="p3tests_")

def fmt(name, m):
    m = np.array(m, dtype=float)
    s = "%s =\n" % name
    for row in m:
        s += "".join("%8.4g " % (v + 0.0) for v in row) + "\n"
    return s + "\n"

def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def compile_prog(src_text, opt, fname="p", func=None):
    p = "%s/%s.matlang" % (TMP, fname)
    c = "%s/%s_%s.c" % (TMP, fname, opt.strip("-"))
    open(p, "w", newline="").write(src_text)
    cmd = [COMP] + ([opt] if opt else []) + [p, c] + ([func] if func else [])
    r = sh(cmd)
    return r, p, c

def first_line(s):
    return s.strip().split("\n")[0] if s.strip() else ""

results = []
def record(id_, cat, desc, inp, expected, actual, ok, extra=None):
    results.append(dict(id=id_, cat=cat, desc=desc, input=inp, expected=expected,
                        actual=actual, status="PASS" if ok else "FAIL", extra=extra or {}))
    if not ok:
        print("FAIL", id_, desc, "\n  expected:", expected, "\n  actual:  ", actual)

def t_run(id_, cat, desc, src, expected, inp=None):
    """valid program: compiled and interpreted, -O0 and -O1, must all print `expected`"""
    outs = {}
    for opt in ("-O1", "-O0"):
        r, p, c = compile_prog(src, opt)
        if r.returncode != 0:
            record(id_, cat, desc, inp or src, "output as expected", "compiler rejected: " + first_line(r.stderr), False); return
        exe = c[:-2]
        g = sh(["gcc", "-Wall", "-I", SRC, c, "-o", exe, "-lm"])
        if g.returncode != 0 or g.stderr.strip():
            record(id_, cat, desc, inp or src, "generated C builds without warnings", "gcc: " + g.stderr[:200], False); return
        e = sh([exe]); outs["compiled " + opt] = e.stdout if e.returncode == 0 else "RUN FAILED: " + e.stderr
        i = sh([INTERP, opt, p]); outs["interpreter " + opt] = i.stdout if i.returncode == 0 else "RUN FAILED: " + i.stderr
    bad = [k for k, v in outs.items() if v != expected]
    record(id_, cat, desc, inp or src, "printed matrices equal the independent answer",
           "all four runs (compiled and interpreted, -O0 and -O1) match" if not bad else "mismatch in: " + ", ".join(bad), not bad,
           {"output": expected})

def t_reject(id_, cat, desc, src, key, inp=None, count=None, extra_check=None, levels=("-O1", "-O0")):
    """invalid program: must exit 1 and the message must contain `key`"""
    firsts = []
    for opt in levels:
        r, p, c = compile_prog(src, opt)
        if r.returncode != 1:
            record(id_, cat, desc, inp or src, "rejected (exit code 1) with: " + key, "exit code %d" % r.returncode, False); return
        if key not in r.stderr:
            record(id_, cat, desc, inp or src, "message contains: " + key, first_line(r.stderr), False); return
        if count is not None and r.stderr.count("error at line") != count:
            record(id_, cat, desc, inp or src, "%d errors reported" % count, "%d reported" % r.stderr.count("error at line"), False); return
        if extra_check and not extra_check(r.stderr):
            record(id_, cat, desc, inp or src, "extra check", r.stderr[:200], False); return
        firsts.append(first_line(r.stderr))
    record(id_, cat, desc, inp or src, "rejected, message: " + key, firsts[0], True)

def t_compile_only(id_, cat, desc, src, inp=None):
    r, p, c = compile_prog(src, "-O1")
    ok = r.returncode == 0
    if ok:
        g = sh(["gcc", "-Wall", "-I", SRC, "-c", c, "-o", c + ".o"]); ok = g.returncode == 0 and not g.stderr.strip()
    record(id_, cat, desc, inp or src, "accepted; generated C builds without warnings",
           "accepted and builds" if ok else first_line(r.stderr), ok)

def t_runtime_error(id_, cat, desc, src, key, inp=None):
    r, p, c = compile_prog(src, "-O1")
    if r.returncode != 0:
        record(id_, cat, desc, inp or src, "compiles, then fails at run time", "compiler rejected: " + first_line(r.stderr), False); return
    exe = c[:-2]
    sh(["gcc", "-I", SRC, c, "-o", exe, "-lm"])
    e = sh([exe])
    i = sh([INTERP, p])
    ok = e.returncode == 1 and key in e.stderr and i.returncode == 1
    record(id_, cat, desc, inp or src, "exit code 1 with: " + key,
           "compiled program: exit %d, '%s'; interpreter: exit %d" % (e.returncode, first_line(e.stderr), i.returncode), ok,
           {"interp_msg": first_line(i.stderr)})

# ---------------------------------------------------------------- VALID INPUTS
A = np.array([[1, 2], [3, 4]])
M = np.array([[1, 2, 3], [4, 5, 6]]); N = np.array([[7, 8], [9, 10], [11, 12]])
t_run("V01", "Valid", "Multiplication of a 2x3 by a 3x2 matrix",
      "A = [[1,2,3],[4,5,6]]\nB = [[7,8],[9,10],[11,12]]\nC = A * B\nprint(C)\n", fmt("C", M @ N))
P = np.array([[0, 1], [1, 0]])
Cm = np.array([[2, 0], [1, 3]])
t_run("V02", "Valid", "Parentheses change the order of evaluation",
      "A = [[1,2],[3,4]]\nB = [[0,1],[1,0]]\nC = [[2,0],[1,3]]\nX = (A + B) * C\nY = A + B * C\nprint(X)\nprint(Y)\n",
      fmt("X", (A + P) @ Cm) + fmt("Y", A + P @ Cm))

# ---------------------------------------------------------------- INVALID INPUT (syntax)
t_reject("I01", "Invalid", "Matrix literal with a missing closing bracket", "A = [[1,2]\nprint(A)\n", "expected ']' to close matrix literal")
t_reject("I02", "Invalid", "add with a single argument", "A = [[1,2],[3,4]]\nB = add(A)\n", "add needs at least 2 arguments, got 1")

# ---------------------------------------------------------------- SEMANTIC ERRORS
t_reject("S01", "Semantic", "Use of an undefined variable", "B = A\n", "undefined variable 'A'")
t_reject("S02", "Semantic", "Adding matrices of different sizes", "A = [[1,2],[3,4]]\nB = [[1,2,3]]\nC = A + B\n", "dimension mismatch for infix '+': 2x2 vs 1x3")

# ---------------------------------------------------------------- BOUNDARY CASES
t_compile_only("B01", "Boundary", "identity(3162): the largest identity allowed (3162 x 3162 = 9,998,244 elements)", "I = identity(3162)\nprint(I)\n")
t_reject("B02", "Boundary", "identity(3163): one more than the element limit", "I = identity(3163)\nprint(I)\n", "a 3163x3163 matrix is too large (limit is 10000000 elements)")

# ---------------------------------------------------------------- RUN-TIME ERROR CONDITIONS
t_runtime_error("R01", "Runtime", "Inverse of a singular matrix", "A = [[1,2],[2,4]]\nB = inverse(A)\nprint(B)\n", "singular")
t_runtime_error("R02", "Runtime", "A singular inverse that is never used is still reported (dead code elimination keeps it)",
                "A = [[1,2],[2,4]]\nB = inverse(A)\n", "singular")

# ---------------------------------------------------------------- COMPLEX INPUTS
Q3 = np.array([[2, 1, 0], [1, 3, 1], [0, 1, 4]], dtype=float)
t_run("C01", "Complex", "Inverse of a 3x3 matrix compared with numpy",
      "A = [[2,1,0],[1,3,1],[0,1,4]]\nI = inverse(A)\nprint(I)\n", fmt("I", np.linalg.inv(Q3)))
t_run("C02", "Complex", "Dead code and repeated expressions in one program",
      "A = [[1,2],[3,4]]\nB = [[0,1],[1,0]]\nU = A * B\nV = U + A\nW = transpose(V)\nX = A * B + A * B\nH = A * identity(2)\nprint(X)\nprint(H)\n",
      fmt("X", 2 * (A @ P)) + fmt("H", A))

if len(sys.argv) > 1:
    json.dump(results, open(sys.argv[1], "w"), indent=1)
shutil.rmtree(TMP, ignore_errors=True)
n = len(results); ok = sum(r["status"] == "PASS" for r in results)
print("tests: %d run, %d passed, %d failed" % (n, ok, n - ok))
sys.exit(0 if ok == n else 1)
