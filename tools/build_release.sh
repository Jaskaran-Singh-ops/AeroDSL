#!/bin/sh
# Builds the prebuilt programs in bin/ for Windows, macOS and Linux from this
# one machine, using Zig's bundled C cross-compiler (pip install ziglang).
# The compiler's sources are plain C11; only src/port.c and src/server.c know
# which system they are on.
#
#   sh tools/build_release.sh        (or: make release)
set -eu
cd "$(dirname "$0")/.."

if ! python3 -m ziglang version >/dev/null 2>&1; then
  echo "needs Zig's C compiler:  pip install ziglang" >&2
  exit 1
fi

SRCS="src/util.c src/diag.c src/lexer.c src/ast.c src/symtab.c src/parser.c
      src/sema.c src/ir.c src/cfg.c src/ssa.c src/opt.c src/analysis.c
      src/codegen.c src/vm.c src/scn.c src/json.c src/compile.c src/port.c
      src/main.c src/server.c"
CC="python3 -m ziglang cc -std=c11 -Wall -Wextra -Wpedantic -O2 -s -Isrc"

build() {  # target  output  extra-flags...
  target=$1; out=$2; shift 2
  mkdir -p "$(dirname "$out")"
  # shellcheck disable=SC2086
  $CC -target "$target" -o "$out" $SRCS "$@"
  echo "built $out"
}

build x86_64-windows-gnu  bin/windows-x64/aerodsl.exe -lws2_32
build aarch64-macos       bin/macos-arm64/aerodsl     -lm
build x86_64-macos        bin/macos-x64/aerodsl       -lm
build x86_64-linux-musl   bin/linux-x64/aerodsl       -static -lm
build aarch64-linux-musl  bin/linux-arm64/aerodsl     -static -lm
rm -f bin/*/*.pdb
