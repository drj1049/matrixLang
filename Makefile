CC     = gcc
CFLAGS = -Wall -Isrc
LDLIBS = -lm

SRCS = $(wildcard src/*.c)
HDRS = $(wildcard src/*.h)

# one test driver per pipeline stage, all built into build/
STAGES = lexer parser semantic optimizer optimizer_prog tac interp
BINS   = build/compiler $(STAGES:%=build/%_test)

GOOD = sample1 sample4_chain sample10_gen sample13_scale sample16_cse sample17_algebra sample21_parens sample23_paren_chain
BAD  = sample2_bad sample3_undefined sample7_bad_input sample8_bad_output sample11_bad_size sample14_bad_number

.PHONY: all test fuzz sanitize run ui clean

all: $(BINS)

build:
	mkdir -p build

# the compiler itself: .matlang in, C out (a main(), or a function plus a
# header when the program has input/output statements)
build/compiler: tests/codegen_test.c $(SRCS) $(HDRS) | build
	$(CC) $(CFLAGS) $< $(SRCS) -o $@ $(LDLIBS)

build/%_test: tests/%_test.c $(SRCS) $(HDRS) | build
	$(CC) $(CFLAGS) $< $(SRCS) -o $@ $(LDLIBS)

# compile a program to C, build it and run it: make run FILE=tests/sample1.matlang
# add OPT=-O0 to switch the optimizer off: make run FILE=... OPT=-O0
# a program with input/output has no main to run, so it stops after writing
# the function: make run FILE=tests/sample5_func.matlang NAME=my_function
run: build/compiler
	@test -n "$(FILE)" || { echo "usage: make run FILE=file.matlang [NAME=function_name]"; exit 1; }
	@name=$$(basename "$(FILE)" .matlang); \
	rm -f "build/$$name.h"; \
	./build/compiler $(OPT) "$(FILE)" "build/$$name.c" $(NAME) || exit 1; \
	if [ -f "build/$$name.h" ]; then \
		echo "wrote build/$$name.c and build/$$name.h:"; \
		grep '^Mat ' "build/$$name.h"; \
	else \
		$(CC) -Isrc "build/$$name.c" -o "build/$$name" $(LDLIBS) && \
		"./build/$$name"; \
	fi

# generated C must print exactly what the interpreter prints,
# and the bad samples must be rejected
test: all
	@for s in $(GOOD); do \
		./build/compiler tests/$$s.matlang build/$$s.c && \
		$(CC) -Isrc build/$$s.c -o build/$$s $(LDLIBS) && \
		./build/$$s > build/$$s.gen.txt && \
		./build/interp_test tests/$$s.matlang > build/$$s.interp.txt && \
		diff build/$$s.gen.txt build/$$s.interp.txt > /dev/null && \
		echo "PASS $$s (compiled output matches interpreter)" || \
		{ echo "FAIL $$s"; exit 1; }; \
	done
	@for s in $(GOOD); do \
		./build/compiler -O0 tests/$$s.matlang build/$$s.O0.c && \
		$(CC) -Isrc build/$$s.O0.c -o build/$$s.O0 $(LDLIBS) && \
		./build/$$s.O0 > build/$$s.O0.txt && \
		diff build/$$s.O0.txt build/$$s.gen.txt > /dev/null && \
		echo "PASS $$s with -O0 (same answer with the optimizer off)" || \
		{ echo "FAIL $$s with -O0"; exit 1; }; \
	done
	@for s in sample16_cse sample17_algebra sample20_cse_func; do \
		./build/compiler tests/$$s.matlang build/$$s.c cse_func && \
		./build/compiler -O0 tests/$$s.matlang build/$$s.O0.c cse_func && \
		a=$$(grep -E -c 'mat_(mul|add|sub|scale|transpose|inverse)\(' build/$$s.c); \
		b=$$(grep -E -c 'mat_(mul|add|sub|scale|transpose|inverse)\(' build/$$s.O0.c); \
		if [ "$$a" -lt "$$b" ]; then echo "PASS $$s: the TAC optimizer cut $$b matrix operations to $$a"; \
		else echo "FAIL $$s: the TAC optimizer removed nothing ($$b -> $$a)"; exit 1; fi; \
	done
	@for s in sample4_chain sample23_paren_chain; do \
		if cmp -s build/$$s.c build/$$s.O0.c; then \
			echo "FAIL -O0 made no difference to the $$s code"; exit 1; \
		else \
			echo "PASS -O0 changes the multiplication order in $$s"; \
		fi; \
	done
	@./build/compiler tests/sample5_func.matlang build/sample5_func.c && \
	./build/compiler tests/sample6_alias.matlang build/sample6_alias.c alias_copy && \
	./build/compiler tests/sample12_gen_func.matlang build/sample12_gen_func.c gen_func && \
	./build/compiler tests/sample15_scale_func.matlang build/sample15_scale_func.c scale_func && \
	./build/compiler tests/sample20_cse_func.matlang build/sample20_cse_func.c cse_func && \
	$(CC) $(CFLAGS) -Ibuild tests/func_caller.c build/sample5_func.c build/sample6_alias.c build/sample12_gen_func.c build/sample15_scale_func.c build/sample20_cse_func.c \
		-o build/func_caller $(LDLIBS) && \
	./build/func_caller && \
	echo "PASS func_caller (generated functions return the right, freshly allocated result)" || \
	{ echo "FAIL func_caller"; exit 1; }
	@if ./build/func_caller badsize 2> build/func_caller.err.txt; then \
		echo "FAIL func_caller (wrong-sized input should have been rejected)"; exit 1; \
	else \
		echo "PASS func_caller (rejected: $$(cat build/func_caller.err.txt))"; \
	fi
	@for s in sample18_many_errors sample19_many_sem_errors; do \
		if ./build/compiler tests/$$s.matlang build/$$s.c 2> build/$$s.err.txt; then \
			echo "FAIL $$s (should have been rejected)"; exit 1; \
		fi; \
		n=$$(grep -c 'error at line' build/$$s.err.txt); \
		if [ "$$n" -ge 3 ]; then echo "PASS $$s (reported $$n errors in one run)"; \
		else echo "FAIL $$s (reported $$n errors, wanted 3)"; exit 1; fi; \
	done
	@for s in sample18_many_errors sample19_many_sem_errors sample22_paren_bad sample24_caret_edge; do \
		./build/compiler tests/$$s.matlang build/$$s.c 2> build/$$s.err.txt; \
		if diff build/$$s.err.txt tests/$$s.expected > /dev/null; then \
			echo "PASS $$s (error messages and carets match tests/$$s.expected)"; \
		else \
			echo "FAIL $$s (error text differs from tests/$$s.expected):"; \
			diff build/$$s.err.txt tests/$$s.expected; exit 1; \
		fi; \
	done
	@for s in $(BAD); do \
		if ./build/compiler tests/$$s.matlang build/$$s.c 2> build/$$s.err.txt; then \
			echo "FAIL $$s (should have been rejected)"; exit 1; \
		else \
			echo "PASS $$s (rejected: $$(head -1 build/$$s.err.txt))"; \
		fi; \
	done

# random programs, compared four ways with an answer worked out in Python
# (interpreter and generated C, optimizer on and off): make fuzz [N=200] [SEED=1]
N ?= 200
fuzz: all
	python3 tests/fuzz.py $(N) $(SEED)

# memory and undefined-behaviour checks: builds the compiler with sanitizers
# and runs it over every sample, good and bad: make sanitize
sanitize: | build
	$(CC) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc tests/codegen_test.c $(SRCS) -o build/compiler_asan $(LDLIBS)
	@fail=0; for f in tests/sample*.matlang; do \
		ASAN_OPTIONS=detect_leaks=0 ./build/compiler_asan $$f build/san.c 2> build/san.err.txt; rc=$$?; \
		if [ $$rc -gt 1 ] || grep -q -E 'Sanitizer|runtime error' build/san.err.txt; then \
			echo "FAIL $$f"; head -5 build/san.err.txt; fail=1; \
		fi; \
	done; \
	if [ $$fail = 0 ]; then echo "PASS sanitizers: no problems in $$(ls tests/sample*.matlang | wc -l | tr -d ' ') samples"; else exit 1; fi

# local web page: make ui, then open http://localhost:8000
ui: build/compiler build/tac_test build/optimizer_prog_test
	python3 server.py

clean:
	rm -rf build
