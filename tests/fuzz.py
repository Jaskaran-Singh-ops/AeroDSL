#!/usr/bin/env python3
"""
Fuzzes the compiler, to establish that no input makes it crash.

Three kinds of input, in equal measure:

  1. random sequences of valid tokens -- grammatically nonsense, lexically
     fine, which is what exercises the parser's error paths;
  2. random bytes, which exercises the lexer's;
  3. valid examples with a handful of random byte edits, which is what finds
     the cases just barely off a correct program -- historically the most
     productive of the three.

An acceptable exit status is 0 (compiled), 1 (diagnostics reported an error)
or 2 (could not read the input). Anything else -- a signal, a sanitiser
report -- is a crash and is printed in full.

Build the binary with sanitisers first, so a latent memory error is a crash
rather than quiet corruption:

    make debug
    python3 tests/fuzz.py ./aerodsl-debug

Defaults to 3000 iterations; pass a count as the second argument.
"""
import os
import random
import subprocess
import sys
import glob

TOKENS = [
    "scenario", "fix", "flight", "type", "enter", "alt", "hdg", "spd", "lat", "lon",
    "proc", "let", "clear", "climb", "descend", "heading", "speed", "direct",
    "advance", "if", "then", "else", "while", "do", "end", "assert", "call",
    "print", "return", "and", "or", "not", "true", "false", "num", "bool",
    "AI202", "BA117", "VOMAN", "FL320", "280KT", "270", "12.95", "x", "y",
    "hsep", "vsep", "dist", "bearing", "now", "abs", "min", "max",
    "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "=",
    "(", ")", ",", ".", '"msg"', "#c\n", "\n",
]


def random_tokens(rnd):
    return " ".join(rnd.choice(TOKENS) for _ in range(rnd.randint(0, 80)))


def random_bytes(rnd):
    return "".join(chr(rnd.randint(1, 127)) for _ in range(rnd.randint(0, 300)))


def mutated(rnd, corpus):
    data = bytearray(rnd.choice(corpus).encode())
    for _ in range(rnd.randint(1, 12)):
        if not data:
            break
        at = rnd.randrange(len(data))
        choice = rnd.randint(0, 2)
        if choice == 0:
            data[at] = rnd.randint(32, 126)
        elif choice == 1:
            del data[at]
        else:
            data.insert(at, rnd.randint(32, 126))
    return data.decode(errors="ignore")


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "./aerodsl"
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
    if not os.path.exists(binary):
        sys.exit(f"{binary} not found; run make first")

    here = os.path.dirname(os.path.abspath(__file__))
    corpus = []
    for path in sorted(glob.glob(os.path.join(here, "..", "examples", "*.aero"))):
        with open(path, errors="ignore") as handle:
            corpus.append(handle.read())
    if not corpus:
        sys.exit("no examples found; run from the project root")

    rnd = random.Random(20260612)
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}
    crashes = 0

    for i in range(rounds):
        kind = i % 3
        if kind == 0:
            source = random_tokens(rnd)
        elif kind == 1:
            source = random_bytes(rnd)
        else:
            source = mutated(rnd, corpus)

        try:
            result = subprocess.run(
                [binary], input=source.encode(), stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, timeout=120, env=env)
        except subprocess.TimeoutExpired:
            crashes += 1
            print(f"TIMEOUT on input:\n{source[:400]!r}\n")
            continue

        if result.returncode not in (0, 1, 2):
            crashes += 1
            print(f"CRASH rc={result.returncode} on input:\n{source[:400]!r}")
            print(result.stderr.decode()[:3000])
            if crashes > 3:
                break

        if (i + 1) % 500 == 0:
            print(f"  {i + 1}/{rounds}", flush=True)

    print(f"{rounds} inputs, {crashes} crashes")
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
