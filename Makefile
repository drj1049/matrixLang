CC     = gcc
CFLAGS = -Wall -Isrc
LDLIBS = -lm

SRCS = $(wildcard src/*.c)
HDRS = $(wildcard src/*.h)

# one test driver per pipeline stage, all built into build/
STAGES = lexer parser semantic optimizer optimizer_prog tac interp
BINS   = build/compiler $(STAGES:%=build/%_test)

GOOD = sample1 sample4_chain
BAD  = sample2_bad sample3_undefined sample7_bad_input sample8_bad_output

.PHONY: all test run clean

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
# a program with input/output has no main to run, so it stops after writing
# the function: make run FILE=tests/sample5_func.matlang NAME=my_function
run: build/compiler
	@test -n "$(FILE)" || { echo "usage: make run FILE=file.matlang [NAME=function_name]"; exit 1; }
	@name=$$(basename "$(FILE)" .matlang); \
	rm -f "build/$$name.h"; \
	./build/compiler "$(FILE)" "build/$$name.c" $(NAME) || exit 1; \
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
	@./build/compiler tests/sample5_func.matlang build/sample5_func.c && \
	./build/compiler tests/sample6_alias.matlang build/sample6_alias.c alias_copy && \
	$(CC) $(CFLAGS) -Ibuild tests/func_caller.c build/sample5_func.c build/sample6_alias.c \
		-o build/func_caller $(LDLIBS) && \
	./build/func_caller && \
	echo "PASS func_caller (generated functions return the right, freshly allocated result)" || \
	{ echo "FAIL func_caller"; exit 1; }
	@if ./build/func_caller badsize 2> build/func_caller.err.txt; then \
		echo "FAIL func_caller (wrong-sized input should have been rejected)"; exit 1; \
	else \
		echo "PASS func_caller (rejected: $$(cat build/func_caller.err.txt))"; \
	fi
	@for s in $(BAD); do \
		if ./build/compiler tests/$$s.matlang build/$$s.c 2> build/$$s.err.txt; then \
			echo "FAIL $$s (should have been rejected)"; exit 1; \
		else \
			echo "PASS $$s (rejected: $$(cat build/$$s.err.txt))"; \
		fi; \
	done

clean:
	rm -rf build
