# AeroDSL -- build with `make`, check with `make test`, demo with `make ui`.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS  += -lm

SRC = src/util.c src/diag.c src/lexer.c src/ast.c src/symtab.c src/parser.c \
      src/sema.c src/ir.c src/cfg.c src/ssa.c src/opt.c src/analysis.c src/codegen.c \
      src/vm.c src/scn.c src/json.c src/compile.c src/port.c
HDR = src/aerodsl.h

all: aerodsl

aerodsl: $(SRC) src/main.c src/server.c $(HDR)
	$(CC) $(CFLAGS) -Isrc -o $@ $(SRC) src/main.c src/server.c $(LDLIBS)

# The test binary links the same sources with its own main.
aerotest: $(SRC) tests/test_all.c $(HDR)
	$(CC) $(CFLAGS) -Isrc -o $@ $(SRC) tests/test_all.c $(LDLIBS)

test: aerotest aerodsl
	./aerotest
	@sh tests/run_examples.sh

# A stricter build used while developing: sanitizers and no optimisation.
debug: $(SRC) src/main.c src/server.c $(HDR)
	$(CC) -std=c11 -Wall -Wextra -Wpedantic -g -O0 -fsanitize=address,undefined \
	      -Isrc -o aerodsl-debug $(SRC) src/main.c src/server.c $(LDLIBS)

# Establishes that no input crashes the compiler. Uses the sanitiser build,
# so a latent memory error shows up as a crash rather than quiet corruption.
fuzz: debug
	python3 tests/fuzz.py ./aerodsl-debug 3000

ui: aerodsl
	./aerodsl --serve 8080

# Prebuilt programs for Windows, macOS and Linux in bin/ (needs: pip install ziglang).
release:
	sh tools/build_release.sh

clean:
	rm -f aerodsl aerotest aerodsl-debug

.PHONY: all test ui clean debug fuzz release
