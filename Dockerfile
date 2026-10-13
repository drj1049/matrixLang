# MatrixLang web page, for hosts that run containers (Render, Railway, Fly.io).
# The server compiles each visitor's program to C and builds it with gcc, so
# the image needs Python, gcc and make.
FROM python:3.12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# run as an ordinary user, not root
RUN useradd --create-home --uid 10001 app
WORKDIR /app
RUN chown app:app /app
COPY --chown=app:app . .
USER app

# build the compiler now, so starting the server is quick
RUN make -s build/compiler build/tac_test build/optimizer_prog_test

# listen on every address, accept any host name, and limit what a visitor's
# program may use (see the top of server.py). The host sets PORT.
ENV HOST=0.0.0.0 \
    MATRIXLANG_PUBLIC=1 \
    NO_BROWSER=1 \
    PYTHONUNBUFFERED=1

CMD ["python3", "server.py"]
