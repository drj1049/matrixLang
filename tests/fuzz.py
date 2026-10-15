#!/usr/bin/env python3
"""Random-program check for the MatrixLang compiler.

    python3 tests/fuzz.py [how_many] [seed]      (or: make fuzz)

Writes random valid programs and runs each one four ways:

    the interpreter with the optimizer off  (-O0)
    the interpreter with the optimizer on   (-O1)
    the generated C with the optimizer off  (-O0)
    the generated C with the optimizer on   (-O1)

and compares all four outputs with the answer worked out here in Python.
Numbers are kept to small whole values, so every result is exact and the
printed text must match character for character. Any difference prints
the program and stops with exit code 1.
"""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILER = os.path.join(ROOT, "build", "compiler")
INTERP = os.path.join(ROOT, "build", "interp_test")
SRC = os.path.join(ROOT, "src")

NAMES = ["A", "B", "C", "D", "E", "F"]
LIMIT = 9999          # keep every number exact when printed with %.4g


class Skip(Exception):
    """This random choice cannot be used with the variables as they are now."""


# ---------- tiny matrix helpers (lists of rows) ----------
def dims(m):
    return len(m), len(m[0])


def mmul(x, y):
    if dims(x)[1] != dims(y)[0]:
        raise Skip()
    return [[sum(x[i][k] * y[k][j] for k in range(len(y))) for j in range(len(y[0]))] for i in range(len(x))]


def madd(x, y, sign):
    if dims(x) != dims(y):
        raise Skip()
    return [[a + sign * b for a, b in zip(r, s)] for r, s in zip(x, y)]


def mscale(k, x):
    return [[k * v for v in r] for r in x]


def mtrans(x):
    return [list(r) for r in zip(*x)]


def minv_upper(x):
    """inverse of [[1,k],[0,1]]"""
    return [[1, -x[0][1]], [0, 1]]


def check_range(m):
    for r in m:
        for v in r:
            if abs(v) > LIMIT:
                raise Skip()
    return m


# ---------- factors: a variable, a literal, identity(n) or zeros(r, c) ----------
class Factor:
    def __init__(self, kind, **kw):
        self.kind = kind
        self.__dict__.update(kw)

    def render(self):
        if self.kind == "var":
            return self.name
        if self.kind == "lit":
            return "[" + ",".join("[" + ",".join(str(v) for v in r) + "]" for r in self.data) + "]"
        if self.kind == "identity":
            return "identity(%d)" % self.n
        return "zeros(%d,%d)" % (self.r, self.c)

    def value(self, env):
        if self.kind == "var":
            if self.name not in env:
                raise Skip()
            return env[self.name]
        if self.kind == "lit":
            return self.data
        if self.kind == "identity":
            return [[1 if i == j else 0 for j in range(self.n)] for i in range(self.n)]
        return [[0] * self.c for _ in range(self.r)]


def rand_literal(rng, r, c):
    return [[rng.randint(-3, 3) for _ in range(c)] for _ in range(r)]


class Item:
    """one operand of a '*' chain: a matrix factor or a number, maybe negated"""
    def __init__(self, factor=None, number=None, neg=False):
        self.factor, self.number, self.neg = factor, number, neg

    def render(self):
        if self.factor is not None:
            body = self.factor.render()
        else:
            body = str(self.number)
        return ("-" if self.neg else "") + body


class Term:
    def __init__(self, items):
        self.items = items

    def render(self):
        return " * ".join(i.render() for i in self.items)

    def value(self, env):
        scalar = 1
        acc = None
        for it in self.items:
            sign = -1 if it.neg else 1
            if it.factor is None:
                scalar *= sign * it.number
            else:
                v = it.factor.value(env)
                if sign < 0:
                    v = mscale(-1, v)
                acc = v if acc is None else mmul(acc, v)
        return check_range(mscale(scalar, acc))


class Expr:
    """terms joined by + and -; every term must have the same size"""
    def __init__(self, first, rest):
        self.first, self.rest = first, rest

    def render(self):
        s = self.first.render()
        for op, t in self.rest:
            s += " %s %s" % (op, t.render())
        return s

    def value(self, env):
        acc = self.first.value(env)
        for op, t in self.rest:
            acc = check_range(madd(acc, t.value(env), 1 if op == "+" else -1))
        return acc


class Group:
    """a parenthesized expression, used wherever a matrix factor can go"""
    kind = "group"

    def __init__(self, expr):
        self.expr = expr

    def render(self):
        return "(" + self.expr.render() + ")"

    def value(self, env):
        return self.expr.value(env)


class Call:
    """add / subtract / multiply of 2 or 3 names, or transpose / inverse of one"""
    def __init__(self, fn, names):
        self.fn, self.names = fn, names

    def render(self):
        return "%s(%s)" % (self.fn, ", ".join(self.names))

    def value(self, env):
        vals = []
        for n in self.names:
            if n not in env:
                raise Skip()
            vals.append(env[n])
        if self.fn == "transpose":
            return mtrans(vals[0])
        if self.fn == "inverse":
            if not upper_unit(vals[0]):
                raise Skip()
            return minv_upper(vals[0])
        acc = vals[0]
        for v in vals[1:]:
            if self.fn == "add":
                acc = madd(acc, v, 1)
            elif self.fn == "subtract":
                acc = madd(acc, v, -1)
            else:
                acc = mmul(acc, v)
        return check_range(acc)


# ---------- building random pieces against the current variables ----------
def upper_unit(x):
    return dims(x) == (2, 2) and x[0][0] == 1 and x[1][0] == 0 and x[1][1] == 1


def build_group(rng, env, r, c, depth):
    """a parenthesized expression of exactly r x c"""
    first = build_term(rng, env, want_dims=(r, c), depth=depth + 1)
    rest = []
    for _ in range(rng.choice([0, 0, 1, 1, 2])):
        rest.append((rng.choice("+-"), build_term(rng, env, want_dims=(r, c), depth=depth + 1)))
    return Group(Expr(first, rest))


def pick_factor(rng, env, need_rows=None, need_cols=None, depth=0):
    """a factor with the wanted number of rows (and columns if given)"""
    if depth < 2 and rng.random() < 0.2:
        r = need_rows if need_rows is not None else (2 if rng.random() < 0.6 else rng.randint(1, 3))
        c = need_cols if need_cols is not None else (2 if rng.random() < 0.6 else rng.randint(1, 3))
        return build_group(rng, env, r, c, depth)
    options = []
    for n, m in env.items():
        r, c = dims(m)
        if (need_rows is None or r == need_rows) and (need_cols is None or c == need_cols):
            options.append(Factor("var", name=n))
    r = need_rows if need_rows is not None else (2 if rng.random() < 0.6 else rng.randint(1, 3))
    c = need_cols if need_cols is not None else (2 if rng.random() < 0.6 else rng.randint(1, 3))
    if r == c and rng.random() < 0.4:
        options.append(Factor("identity", n=r))
    if rng.random() < 0.3:
        options.append(Factor("zeros", r=r, c=c))
    options.append(Factor("lit", data=rand_literal(rng, r, c)))
    # prefer variables, so the same pieces come back and give common subexpressions
    vars_only = [o for o in options if o.kind == "var"]
    if vars_only and rng.random() < 0.7:
        return rng.choice(vars_only)
    return rng.choice(options)


def build_term(rng, env, want_dims=None, depth=0):
    n_mats = rng.choice([1, 1, 2, 2, 3, 4])
    items = []
    if want_dims is not None:
        rows, cols = want_dims
    else:
        rows, cols = None, None
    cur_rows = rows
    for k in range(n_mats):
        last = k == n_mats - 1
        f = pick_factor(rng, env, need_rows=cur_rows, need_cols=cols if last else None, depth=depth)
        m = f.value(env)
        items.append(Item(factor=f, neg=rng.random() < 0.15))
        cur_rows = dims(m)[1]
    for _ in range(rng.choice([0, 0, 0, 1, 1, 2])):
        pos = rng.randint(0, len(items))
        items.insert(pos, Item(number=rng.choice([0, 1, 2, 3, 2, 1]), neg=rng.random() < 0.3))
    return Term(items)


def build_rhs(rng, env, inv_ok):
    r = rng.random()
    names = list(env)
    if r < 0.12 and names:
        fn = rng.choice(["add", "subtract"])
        a = rng.choice(names)
        same = [n for n in names if dims(env[n]) == dims(env[a])]
        k = rng.choice([2, 2, 3])
        return Call(fn, [a] + [rng.choice(same) for _ in range(k - 1)])
    if r < 0.2 and names:
        a = rng.choice(names)
        cands = [n for n in names if dims(env[a])[1] == dims(env[n])[0]]
        b = rng.choice(cands)
        cands2 = [n for n in names if dims(env[b])[1] == dims(env[n])[0]]
        chain = [a, b] + ([rng.choice(cands2)] if rng.random() < 0.5 else [])
        return Call("multiply", chain)
    if r < 0.3 and names:
        return Call("transpose", [rng.choice(names)])
    if r < 0.5 and inv_ok:
        return Call("inverse", [rng.choice(sorted(inv_ok))])
    # an expression: one to three terms of the same size
    first = build_term(rng, env)
    want = dims(first.value(env))
    rest = []
    for _ in range(rng.choice([0, 0, 1, 1, 2])):
        rest.append((rng.choice("+-"), build_term(rng, env, want_dims=want)))
    return Expr(first, rest)


def mirror(rhs):
    """the same pieces in the opposite order (A - B becomes B - A, A * B becomes B * A)"""
    if isinstance(rhs, Expr) and len(rhs.rest) == 1:
        op, t2 = rhs.rest[0]
        return Expr(t2, [(op, rhs.first)])
    if isinstance(rhs, Expr) and not rhs.rest:
        mats = [it for it in rhs.first.items if it.factor is not None]
        if len(mats) >= 2:
            return Expr(Term(list(reversed(rhs.first.items))), [])
    if isinstance(rhs, Call) and rhs.fn in ("add", "subtract", "multiply") and len(rhs.names) == 2:
        return Call(rhs.fn, list(reversed(rhs.names)))
    return rhs


# ---------- one program ----------
def fmt(name, m):
    out = ["%s =\n" % name]
    for r in m:
        out.append("".join("%8.4g " % (v + 0.0) for v in r) + "\n")
    out.append("\n")
    return "".join(out)


def make_program(rng):
    lines, env, inv_ok = [], {}, set()
    expected = []
    made = []                      # right-hand sides written so far, to repeat on purpose
    steps = rng.randint(4, 14)
    for _ in range(steps):
        # sometimes a fresh invertible matrix, so inverse(inverse(P)) can show up
        if rng.random() < 0.12:
            name = rng.choice(NAMES)
            k = rng.randint(-3, 3)
            lines.append("%s = [[1,%d],[0,1]]" % (name, k))
            env[name] = [[1, k], [0, 1]]
            inv_ok.add(name)
            continue
        try:
            roll = rng.random()
            if made and roll < 0.25:
                rhs = rng.choice(made)                 # the same expression again
            elif made and roll < 0.4:
                rhs = mirror(rng.choice(made))         # the same pieces the other way round
            else:
                rhs = build_rhs(rng, env, inv_ok)
            val = rhs.value(env)
        except (Skip, IndexError):
            continue
        made.append(rhs)
        name = rng.choice(NAMES)
        lines.append("%s = %s" % (name, rhs.render()))
        env[name] = val
        # keep track of which names hold an invertible matrix
        inv_ok.discard(name)
        if isinstance(rhs, Call) and rhs.fn == "inverse" and upper_unit(val):
            inv_ok.add(name)
        if rng.random() < 0.3:
            lines.append("print(%s)" % name)
            expected.append(fmt(name, val))
    # half of the programs print only some of the final variables, so the rest
    # (and everything that only fed them) is dead code for the optimizer
    only_some = rng.random() < 0.5
    for n in sorted(env):
        if only_some and rng.random() < 0.5:
            continue
        lines.append("print(%s)" % n)
        expected.append(fmt(n, env[n]))
    return "\n".join(lines) + "\n", "".join(expected)


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, **kw)


def check(prog, expected, tmp, label):
    src = os.path.join(tmp, "p.matlang")
    with open(src, "w") as f:
        f.write(prog)
    results = {}
    for flag in ("-O0", "-O1"):
        p = run([INTERP, flag, src])
        results["interpreter " + flag] = p.stdout if p.returncode == 0 else "FAILED: " + p.stderr
        c = os.path.join(tmp, "p.c")
        exe = os.path.join(tmp, "p")
        p = run([COMPILER, flag, src, c])
        if p.returncode != 0:
            results["compiled " + flag] = "COMPILE FAILED: " + p.stderr
            continue
        p = run(["gcc", "-I", SRC, c, "-o", exe, "-lm"])
        if p.returncode != 0:
            results["compiled " + flag] = "GCC FAILED: " + p.stderr
            continue
        p = run([exe])
        results["compiled " + flag] = p.stdout if p.returncode == 0 else "RUN FAILED: " + p.stderr
    bad = [k for k, v in results.items() if v != expected]
    if bad:
        print("MISMATCH in program %s for: %s\n" % (label, ", ".join(bad)))
        print(prog)
        print("--- expected\n" + expected)
        for k in bad:
            print("--- " + k + "\n" + results[k])
        return False
    return True


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else random.randrange(10 ** 6)
    for tool in (COMPILER, INTERP):
        if not os.path.exists(tool):
            sys.exit("build the tools first: make all")
    rng = random.Random(seed)
    done = 0
    with tempfile.TemporaryDirectory(prefix="matlang_fuzz_") as tmp:
        for n in range(count):
            prog, expected = make_program(rng)
            if not expected:
                continue
            if not check(prog, expected, tmp, "#%d (seed %d)" % (n, seed)):
                sys.exit(1)
            done += 1
    print("fuzz: %d random programs agree four ways with the Python answer (seed %d)" % (done, seed))


if __name__ == "__main__":
    main()
