"""Starts a program with limits on its memory, CPU time and file size.

    python3 runlimited.py MEMORY_BYTES CPU_SECONDS FILE_BYTES program [arguments...]

server.py uses this when it runs strangers' programs on the internet. It is a
separate little program (not code run inside the server) because starting a
process from a program with several threads is not safe if code has to run
between the fork and the start of the new program.
"""
import os
import signal
import sys

try:
    import resource
except ImportError:      # not a system with these limits
    resource = None

if len(sys.argv) < 5:
    sys.exit("usage: runlimited.py MEMORY_BYTES CPU_SECONDS FILE_BYTES program [arguments...]")

memory, cpu, size = (int(v) for v in sys.argv[1:4])
if resource is not None:
    for which, value in ((resource.RLIMIT_AS, memory),
                         (resource.RLIMIT_CPU, cpu),
                         (resource.RLIMIT_FSIZE, size)):
        try:
            resource.setrlimit(which, (value, value))
        except (ValueError, OSError):
            pass         # this system does not allow that limit: carry on without it

# Python itself ignores "file too big" signals, and the new program would inherit
# that: restore the default, so a program that writes too much is stopped
if hasattr(signal, "SIGXFSZ"):
    signal.signal(signal.SIGXFSZ, signal.SIG_DFL)

try:
    os.execvp(sys.argv[4], sys.argv[4:])
except OSError as e:
    sys.exit("could not start %s: %s" % (sys.argv[4], e))
