#!/usr/bin/env python3
"""Web page for the MatrixLang compiler.

    python3 server.py        then open http://localhost:8000

By default it only listens on this computer. For every Run it saves the
program to a temporary folder, calls the programs that make already built in
build/ (the compiler, the TAC printer and the optimizer printer), and for
normal programs also compiles and runs the generated C with gcc.

To put it on the internet (see the Dockerfile), these environment variables
change how it behaves:

    HOST=0.0.0.0              listen on every network address, not just this computer
    PORT=10000                the port to listen on (hosts such as Render set this)
    MATRIXLANG_PUBLIC=1       accept any host name, and apply the limits below
                              to strangers' programs (time, memory, output size,
                              how many runs at once, how many runs per minute)
    NO_BROWSER=1              do not open a browser window at start
"""
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

try:
    import resource            # Linux and macOS only
except ImportError:
    resource = None

ROOT = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(ROOT, "web")
TESTS = os.path.join(ROOT, "tests")
HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "8000"))
PUBLIC = os.environ.get("MATRIXLANG_PUBLIC") == "1"

MAX_CODE = 20000        # characters of MatrixLang source per run
MAX_BODY = 100000       # bytes of request
TOOL_TIMEOUT = 10       # seconds for the compiler stages and gcc
RUN_TIMEOUT = 5         # seconds for the compiled program

# limits for every program the server starts (the compiler stages, gcc and the
# compiled program), so one visitor cannot use up the whole machine
TOOL_MEMORY = 768 * 1024 * 1024    # bytes of memory for the compiler stages and gcc
RUN_MEMORY = 256 * 1024 * 1024     # bytes of memory for the compiled program
RUN_OUTPUT = 1024 * 1024           # the compiled program may write at most this much
SHOW_OUTPUT = 200000               # characters of its output sent to the page
MAX_AT_ONCE = 2                    # runs happening at the same moment
WAIT_FOR_SLOT = 20                 # seconds a run waits for its turn before giving up
RATE_RUNS = 20                     # runs allowed per visitor ...
RATE_WINDOW = 60                   # ... in this many seconds (public mode only)

slots = threading.BoundedSemaphore(MAX_AT_ONCE)
recent_runs = {}                   # visitor address -> times of their latest runs
recent_lock = threading.Lock()

COMPILER = os.path.join(ROOT, "build", "compiler")
TAC_TOOL = os.path.join(ROOT, "build", "tac_test")
OPT_TOOL = os.path.join(ROOT, "build", "optimizer_prog_test")
FUNC_NAME = "matlang_compute"


def run(cmd, cwd=None, timeout=TOOL_TIMEOUT, memory=TOOL_MEMORY, stdout_file=None, file_bytes=16 * 1024 * 1024):
    """Returns (exit_code, stdout, stderr). Exit code 124 means timed out.

    The program runs in its own process group, so a timeout also stops
    anything it started (gcc starts several helper programs). On a public
    server it is started through runlimited.py, which caps its memory, CPU
    time and file size. With stdout_file the output goes to that file instead
    of being collected, so a program that prints without end hits the file
    size limit instead of filling the server's memory."""
    if PUBLIC and resource is not None:
        cmd = [sys.executable, "-S", os.path.join(ROOT, "runlimited.py"),
               str(memory), str(timeout + 1), str(file_bytes)] + cmd
    out = open(stdout_file, "wb") if stdout_file else subprocess.PIPE
    try:
        p = subprocess.Popen(cmd, cwd=cwd, stdout=out, stderr=subprocess.PIPE,
                             universal_newlines=not stdout_file,
                             start_new_session=True)
        try:
            stdout, stderr = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except OSError:
                pass
            p.communicate()
            return 124, "", "timed out after %d seconds" % timeout
        if stdout_file:
            stdout, stderr = "", stderr.decode("utf-8", "replace")
        return p.returncode, stdout, stderr
    finally:
        if stdout_file:
            out.close()


def build_tools():
    """Make sure the three helper programs exist and are up to date."""
    try:
        p = subprocess.run(
            ["make", "-s", "build/compiler", "build/tac_test", "build/optimizer_prog_test"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            universal_newlines=True)
    except FileNotFoundError:
        sys.exit("make is not installed, so the compiler could not be built.")
    if p.returncode != 0:
        sys.exit("Could not build the compiler:\n" + p.stdout)


def error_lines(text):
    """Every source line the compiler complained about, in order, without repeats."""
    found = re.findall(r"^(?:Parse|Semantic) error at line (\d+)", text, re.MULTILINE)
    if not found:
        m = re.search(r"line (\d+)", text, re.IGNORECASE)
        found = [m.group(1)] if m else []
    lines = []
    for n in found:
        if int(n) not in lines:
            lines.append(int(n))
    return lines


def run_pipeline(code, optimize=True):
    """Runs one program, but only MAX_AT_ONCE at the same moment."""
    if not slots.acquire(timeout=WAIT_FOR_SLOT):
        return {"ok": False, "error": "The server is busy right now. Try again in a few seconds."}
    try:
        return run_one(code, optimize)
    finally:
        slots.release()


def run_one(code, optimize=True):
    started = time.time()
    if not code.strip():
        return {"ok": False, "error": "Write a program first."}
    if len(code) > MAX_CODE:
        return {"ok": False, "error": "Program is too long (limit is %d characters)." % MAX_CODE}

    tmp = tempfile.mkdtemp(prefix="matlang_")
    try:
        src = os.path.join(tmp, "prog.matlang")
        c_path = os.path.join(tmp, "prog.c")
        h_path = os.path.join(tmp, "prog.h")
        with open(src, "w") as f:
            f.write(code if code.endswith("\n") else code + "\n")

        flag = [] if optimize else ["-O0"]
        rc, out, err = run([COMPILER] + flag + [src, c_path, FUNC_NAME])
        if rc != 0:
            msg = (err or out).strip() or "The compiler stopped without a message."
            if rc < 0:
                msg = "The compiler crashed on this program.\n" + msg
            lines = error_lines(msg)
            return {"ok": False, "error": msg, "line": lines[0] if lines else None, "lines": lines}

        with open(c_path) as f:
            c_code = f.read()
        header = None
        if os.path.exists(h_path):
            with open(h_path) as f:
                header = f.read()

        rc, out, err = run([TAC_TOOL] + flag + [src])
        tac_part, _, notes_part = out.partition("\n=== notes ===\n")
        tac = tac_part.split("\n\n", 1)[1] if tac_part.startswith("Three-address code:") and "\n\n" in tac_part else tac_part
        tac = tac.strip("\n")
        notes = []      # what the TAC optimizer reused or simplified
        for ln in notes_part.splitlines():
            f = ln.split("\t")
            if len(f) == 3 and f[0] in ("REUSE", "REWRITE") and f[1].isdigit():
                notes.append({"kind": f[0].lower(), "line": int(f[1]), "text": f[2]})

        before = after = ""
        chains = []
        rc, out, err = run([OPT_TOOL, src])
        if rc == 0 and "=== before ===" in out and "=== after ===" in out:
            head, tail = out.split("=== after ===", 1)
            before_part, _, chain_part = head.partition("=== chains ===")
            before = before_part.replace("=== before ===", "").strip("\n")
            after = tail.strip("\n")
            if not optimize:
                after = before      # nothing was rewritten
                chain_part = ""     # and no multiplication order was chosen
            for ln in chain_part.splitlines():
                f = ln.split("\t")
                if len(f) != 7 or f[0] != "CHAIN":
                    continue
                operands = []
                for item in f[2].split(","):
                    name, _, dims = item.rpartition(":")
                    rows, _, cols = dims.partition("x")
                    operands.append({"name": name, "rows": int(rows), "cols": int(cols)})
                chains.append({"line": int(f[1]), "operands": operands,
                               "written": f[3], "written_cost": int(f[4]),
                               "best": f[5], "best_cost": int(f[6])})

        result = {"ok": True, "optimized": optimize, "mode": "function" if header else "program",
                  "c": c_code, "header": header, "tac": tac,
                  "before": before, "after": after, "chains": chains, "notes": notes, "output": None}

        if not header:
            exe = os.path.join(tmp, "prog")
            rc, out, err = run(["gcc", "-I", os.path.join(ROOT, "src"), c_path,
                                "-o", exe, "-lm"])
            if rc != 0:
                return {"ok": False, "error": "gcc could not build the generated C:\n" + err.strip()}
            out_path = os.path.join(tmp, "prog.out")
            rc, _, err = run([exe], timeout=RUN_TIMEOUT, memory=RUN_MEMORY,
                             stdout_file=out_path, file_bytes=RUN_OUTPUT)
            with open(out_path, "rb") as f:
                out = f.read(SHOW_OUTPUT + 1).decode("utf-8", "replace")
            too_big = os.path.getsize(out_path) >= RUN_OUTPUT
            truncated = len(out) > SHOW_OUTPUT
            result["output"] = out[:SHOW_OUTPUT] + ("\n... (output cut off here)\n" if truncated else "")
            if rc == 124:
                result["run_error"] = "The program took longer than %d seconds, so it was stopped." % RUN_TIMEOUT
            elif rc == -signal.SIGXFSZ or too_big:
                result["run_error"] = "The program printed too much output, so it was stopped."
            elif rc in (-signal.SIGXCPU, -signal.SIGKILL):
                result["run_error"] = "The program used too much time or memory, so it was stopped."
            elif rc < 0:
                result["run_error"] = "The program crashed (signal %d)." % -rc
            elif rc != 0:
                result["run_error"] = (err.strip() or "The program exited with code %d." % rc)

        result["ms"] = int((time.time() - started) * 1000)
        return result
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def list_samples():
    samples = []
    if os.path.isdir(TESTS):
        for name in sorted(os.listdir(TESTS)):
            if name.startswith("sample") and name.endswith(".matlang"):
                with open(os.path.join(TESTS, name)) as f:
                    samples.append({"name": name[:-len(".matlang")], "code": f.read()})
    return samples


class Handler(BaseHTTPRequestHandler):
    server_version = "MatrixLangUI"
    timeout = 15        # seconds a visitor may take to send a request

    def log_message(self, fmt, *args):
        pass  # keep the terminal quiet

    def _allowed(self):
        """On your own computer only this computer's own page may talk to the
        server. On a public server any host name is fine, but a request sent
        from some other website's page is refused."""
        host = self.headers.get("Host") or ""
        origin = self.headers.get("Origin")
        if PUBLIC:
            return not origin or urlparse(origin).netloc == host
        if host.split(":")[0] not in ("localhost", "127.0.0.1"):
            return False
        if origin and origin not in ("http://localhost:%d" % PORT, "http://127.0.0.1:%d" % PORT):
            return False
        return True

    def _too_many_runs(self):
        """True when this visitor has used up their runs for the moment (public server only)."""
        if not PUBLIC:
            return False
        who = (self.headers.get("X-Forwarded-For") or self.client_address[0]).split(",")[0].strip()
        now = time.time()
        with recent_lock:
            times = [t for t in recent_runs.get(who, []) if now - t < RATE_WINDOW]
            if len(times) >= RATE_RUNS:
                recent_runs[who] = times
                return True
            times.append(now)
            recent_runs[who] = times
            if len(recent_runs) > 5000:      # forget visitors who have gone quiet
                for k in [k for k, v in recent_runs.items() if not v or now - v[-1] >= RATE_WINDOW]:
                    del recent_runs[k]
        return False

    def _send(self, status, body, ctype):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status, obj):
        self._send(status, json.dumps(obj), "application/json; charset=utf-8")

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/healthz":          # the hosting service asks this to see the server is alive
            return self._send(200, "ok", "text/plain; charset=utf-8")
        if not self._allowed():
            return self._json(403, {"error": "forbidden"})
        if path in ("/", "/index.html"):
            with open(os.path.join(WEB, "index.html"), "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        if path == "/api/samples":
            return self._json(200, list_samples())
        self._json(404, {"error": "not found"})

    def do_POST(self):
        if not self._allowed():
            return self._json(403, {"error": "forbidden"})
        if self.path.split("?")[0] != "/api/run":
            return self._json(404, {"error": "not found"})
        if "application/json" not in (self.headers.get("Content-Type") or ""):
            return self._json(415, {"ok": False, "error": "expected JSON"})
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        if length <= 0 or length > MAX_BODY:
            return self._json(413, {"ok": False, "error": "request too large"})
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            code = body.get("code", "")
            optimize = body.get("optimize", True)
        except (ValueError, AttributeError):
            return self._json(400, {"ok": False, "error": "bad request"})
        if not isinstance(code, str) or not isinstance(optimize, bool):
            return self._json(400, {"ok": False, "error": "bad request"})
        if self._too_many_runs():
            return self._json(429, {"ok": False, "error": "Too many runs from your address. Wait a minute and try again."})
        self._json(200, run_pipeline(code, optimize))


def main():
    build_tools()
    try:
        httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError:
        sys.exit("Port %d is busy. Try: PORT=8001 python3 server.py" % PORT)
    url = "http://localhost:%d" % PORT
    if HOST in ("127.0.0.1", "localhost"):
        print("MatrixLang UI running at %s  (Ctrl+C to stop)" % url)
    else:
        print("MatrixLang UI listening on %s:%d  (Ctrl+C to stop)" % (HOST, PORT), flush=True)
    if not os.environ.get("NO_BROWSER") and not PUBLIC:
        try:
            webbrowser.open(url)
        except Exception:
            pass
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
