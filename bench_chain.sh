#!/bin/sh
# Benchmark for the matrix chain optimizer.
# D = A * B * C with A 1000x20, B 20x1000, C 1000x20, built with the optimizer off (-O0) and on (-O1).
# Run it from the project folder:  bash bench_chain.sh
set -e
cd "$(dirname "$0")"
make -s > /dev/null
mkdir -p build

python3 - <<'PY'
import random
rg = random.Random(5)
def lit(r, c):
    rows = ["[" + ",".join(str(rg.randint(-3, 3)) for _ in range(c)) + "]" for _ in range(r)]
    return "[" + ",".join(rows) + "]"
prog = "A = %s\nB = %s\nC = %s\nD = A * B * C\nprint(D)\n" % (lit(1000, 20), lit(20, 1000), lit(1000, 20))
open("build/bigchain.matlang", "w").write(prog)
PY

for level in 0 1; do
    ./build/compiler -O$level build/bigchain.matlang build/big$level.c
    gcc -I src build/big$level.c -o build/big$level -lm
done

python3 - <<'PY'
import os, re, statistics, subprocess, sys, time
def run(exe):
    times = []
    for _ in range(9):
        t = time.perf_counter()
        subprocess.run([exe], stdout=subprocess.DEVNULL, check=True)
        times.append((time.perf_counter() - t) * 1000)
    # peak memory of one run, from /usr/bin/time (macOS prints bytes, Linux prints KB)
    try:
        if sys.platform == "darwin":
            err = subprocess.run(["/usr/bin/time", "-l", exe], stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, text=True).stderr
            peak_mb = int(re.search(r"(\d+)\s+maximum resident set size", err).group(1)) / (1024 * 1024)
        else:
            err = subprocess.run(["/usr/bin/time", "-f", "%M", exe], stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, text=True).stderr
            peak_mb = int(err.strip().split()[-1]) / 1024
    except Exception:
        peak_mb = float("nan")
    return statistics.median(times), peak_mb
out0 = subprocess.run(["build/big0"], capture_output=True).stdout
out1 = subprocess.run(["build/big1"], capture_output=True).stdout
print("same output from both builds:", out0 == out1, "(%d bytes)" % len(out0))
for name, exe in (("big0  optimizer off (-O0)", "build/big0"), ("big1  optimizer on  (-O1)", "build/big1")):
    ms, mb = run(os.path.abspath(exe))
    print("%s: median %.1f ms of 9 runs, peak memory %.1f MB" % (name, ms, mb))
PY
