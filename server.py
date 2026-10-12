#!/usr/bin/env python3
"""Local web page for the MatrixLang compiler.

    python3 server.py        then open http://localhost:8000

It only listens on this computer. For every Run it saves the program to a
temporary folder, calls the programs that make already built in build/
(the compiler, the TAC printer and the optimizer printer), and for normal
programs also compiles and runs the generated C with gcc.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(ROOT, "web")
TESTS = os.path.join(ROOT, "tests")
HOST = "127.0.0.1"
PORT = int(os.environ.get("PORT", "8000"))

MAX_CODE = 20000        # characters of MatrixLang source per run
MAX_BODY = 100000       # bytes of request
TOOL_TIMEOUT = 10       # seconds for the compiler stages and gcc
RUN_TIMEOUT = 5         # seconds for the compiled program

COMPILER = os.path.join(ROOT, "build", "compiler")
TAC_TOOL = os.path.join(ROOT, "build", "tac_test")
OPT_TOOL = os.path.join(ROOT, "build", "optimizer_prog_test")
FUNC_NAME = "matlang_compute"


def run(cmd, cwd=None, timeout=TOOL_TIMEOUT):
    """Returns (exit_code, stdout, stderr). Exit code 124 means timed out."""
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, universal_newlines=True,
                           timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return 124, "", "timed out after %d seconds" % timeout


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


def error_line(text):
    m = re.search(r"line (\d+)", text, re.IGNORECASE)
    return int(m.group(1)) if m else None


def run_pipeline(code):
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

        rc, out, err = run([COMPILER, src, c_path, FUNC_NAME])
        if rc != 0:
            msg = (err or out).strip() or "The compiler stopped without a message."
            if rc < 0:
                msg = "The compiler crashed on this program.\n" + msg
            return {"ok": False, "error": msg, "line": error_line(msg)}

        with open(c_path) as f:
            c_code = f.read()
        header = None
        if os.path.exists(h_path):
            with open(h_path) as f:
                header = f.read()

        rc, out, err = run([TAC_TOOL, src])
        tac = out.split("\n\n", 1)[1] if out.startswith("Three-address code:") and "\n\n" in out else out
        tac = tac.strip("\n")

        before = after = ""
        chains = []
        rc, out, err = run([OPT_TOOL, src])
        if rc == 0 and "=== before ===" in out and "=== after ===" in out:
            head, tail = out.split("=== after ===", 1)
            before_part, _, chain_part = head.partition("=== chains ===")
            before = before_part.replace("=== before ===", "").strip("\n")
            after = tail.strip("\n")
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

        result = {"ok": True, "mode": "function" if header else "program",
                  "c": c_code, "header": header, "tac": tac,
                  "before": before, "after": after, "chains": chains, "output": None}

        if not header:
            exe = os.path.join(tmp, "prog")
            rc, out, err = run(["gcc", "-I", os.path.join(ROOT, "src"), c_path,
                                "-o", exe, "-lm"])
            if rc != 0:
                return {"ok": False, "error": "gcc could not build the generated C:\n" + err.strip()}
            rc, out, err = run([exe], timeout=RUN_TIMEOUT)
            result["output"] = out
            if rc != 0:
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

    def log_message(self, fmt, *args):
        pass  # keep the terminal quiet

    def _allowed(self):
        """Only this computer's own page may talk to the server."""
        host = (self.headers.get("Host") or "").split(":")[0]
        if host not in ("localhost", "127.0.0.1"):
            return False
        origin = self.headers.get("Origin")
        if origin and origin not in ("http://localhost:%d" % PORT, "http://127.0.0.1:%d" % PORT):
            return False
        return True

    def _send(self, status, body, ctype):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status, obj):
        self._send(status, json.dumps(obj), "application/json; charset=utf-8")

    def do_GET(self):
        if not self._allowed():
            return self._json(403, {"error": "forbidden"})
        path = self.path.split("?")[0]
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
            code = json.loads(self.rfile.read(length).decode("utf-8")).get("code", "")
        except (ValueError, AttributeError):
            return self._json(400, {"ok": False, "error": "bad request"})
        if not isinstance(code, str):
            return self._json(400, {"ok": False, "error": "bad request"})
        self._json(200, run_pipeline(code))


def main():
    build_tools()
    try:
        httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    except OSError:
        sys.exit("Port %d is busy. Try: PORT=8001 python3 server.py" % PORT)
    url = "http://localhost:%d" % PORT
    print("MatrixLang UI running at %s  (Ctrl+C to stop)" % url)
    if not os.environ.get("NO_BROWSER"):
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
