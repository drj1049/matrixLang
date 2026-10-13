#!/usr/bin/env python3
"""Checks the web server the way it runs on the internet (make servercheck).

Starts server.py in public mode on a free port, then checks that it answers,
refuses pages from other websites, runs a normal program, stops a program that
runs too long or prints too much, and limits how often one visitor can run.
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PORT = free_port()
BASE = "http://127.0.0.1:%d" % PORT
failures = []


def post(code, who="1.1.1.1", origin=None):
    """returns (status, answer) for one run request, as visitor `who`"""
    headers = {"Content-Type": "application/json", "X-Forwarded-For": who}
    if origin:
        headers["Origin"] = origin
    req = urllib.request.Request(BASE + "/api/run", data=json.dumps({"code": code, "optimize": True}).encode(),
                                 headers=headers)
    try:
        r = urllib.request.urlopen(req, timeout=60)
        return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))
    if not ok:
        failures.append(name)


def main():
    env = dict(os.environ, HOST="127.0.0.1", PORT=str(PORT), MATRIXLANG_PUBLIC="1", NO_BROWSER="1")
    server = subprocess.Popen([sys.executable, os.path.join(ROOT, "server.py")], cwd=ROOT, env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                if urllib.request.urlopen(BASE + "/healthz", timeout=1).read() == b"ok":
                    break
            except OSError:
                time.sleep(0.2)
        else:
            sys.exit("the server did not start")
        check("server answers /healthz", True)

        page = urllib.request.urlopen(BASE + "/").read()
        check("server serves the page", b"MatrixLang" in page)

        st, d = post("A = [[1,2],[3,4]]\nB = (A + A) * A\nprint(B)\n")
        check("normal program runs", st == 200 and d.get("ok") and "14" in (d.get("output") or "") and "44" in d["output"], d)

        st, d = post("A = [[1]]\nprint(A)\n", origin="https://some-other-site.example")
        check("a page from another website is refused", st == 403, (st, d))

        st, d = post("A = [[1,2]]\nB = A * A\n")
        check("a mistake in a program gives an error, not a crash", st == 200 and d.get("ok") is False and "^" in d.get("error", ""), d)

        st, d = post("X = identity(2000) + identity(2000)\nY = X * X * X * X * X\nprint(Y)\n", who="2.2.2.2")
        check("a program that runs too long is stopped", d.get("ok") and "longer than" in d.get("run_error", ""), d.get("run_error"))

        st, d = post("X = identity(3000) + identity(3000)\nA = X + X\nB = X - A\nC = X + B\nD = A + C\nE = D + A\nprint(E)\n", who="3.3.3.3")
        check("a program that needs too much memory is stopped", d.get("ok") and "out of memory" in d.get("run_error", ""), d.get("run_error"))

        st, d = post("X = identity(1000) + identity(1000)\nprint(X)\n", who="4.4.4.4")
        check("a program that prints too much is stopped",
              d.get("ok") and "too much output" in d.get("run_error", "") and len(d["output"]) < 250000, d.get("run_error"))

        codes = [post("A = [[1]]\nprint(A)\n", who="5.5.5.5")[0] for _ in range(25)]
        check("one visitor cannot run more than 20 times a minute", codes[:20] == [200] * 20 and codes[-1] == 429, codes)
    finally:
        server.terminate()
        server.wait(timeout=10)
    if failures:
        sys.exit(1)
    print("PASS server (public mode): all checks")


if __name__ == "__main__":
    main()
