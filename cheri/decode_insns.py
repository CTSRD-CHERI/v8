#!/usr/bin/env python3

"""
Decode the faulting instructions reported by a CHERI fault dump.

The handler can only print the raw encoding word ("insn=0x38636842"), so this
collects the distinct words from logs and resolves them in one llvm-mc pass.
llvm-mc has to be a Morello build, and is taken from --llvm-mc, then
$CHERI_LLVM_MC, then PATH. Either of the first two may name a command rather
than a program, so an sdk on another host works:

  CHERI_LLVM_MC='ssh buildhost /path/to/morello-sdk/bin/llvm-mc'

Usage:
  cheri/decode_insns.py 14_5/combined_results.log
  cheri/decode_insns.py --llvm-mc llvm-mc < d8.stderr
  cheri/decode_insns.py --in-place 14_5/combined_results.log

--in-place rewrites the log, which is what distribute_v8_tests.py runs after
combining the per-machine logs.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

INSN_RE = re.compile(r"insn=0x([0-9a-fA-F]+)")
# The raw form of an insn= field, which is always last on its line. Matching it
# to the end of the line means an already annotated log is left as it is.
RAW_INSN_RE = re.compile(r"insn=0x([0-9a-fA-F]+)$", re.MULTILINE)
# Runner banner introducing each test's output block.
TEST_RE = re.compile(r"^=== (\S+) ===$")
# llvm-mc echoes the bytes it decoded, which maps an output line back to its
# word without depending on line order.
ENCODING_RE = re.compile(r"// encoding: \[([^\]]+)\]")
BYTE_RE = re.compile(r"0x([0-9a-f]{2})")

NOT_DISASSEMBLED = "<not disassembled>"

# Only a Morello-built llvm-mc knows these encodings. A stock one reports the
# mattrs as unrecognised and decodes nothing, which is checked below.
LLVM_MC_FLAGS = (
    "-triple=aarch64 -mattr=+morello,+c64" " -disassemble -show-encoding"
)


def collect(counts: dict[str, int], tests: dict[str, str], text: str) -> None:
    """Count each instruction word and remember one test that hit it."""
    banner = None
    for line in text.splitlines():
        match = TEST_RE.match(line)
        if match:
            banner = match.group(1)
            continue
        for found in INSN_RE.finditer(line):
            word = found.group(1).lstrip("0")
            counts[word] = counts.get(word, 0) + 1
            if banner and word not in tests:
                tests[word] = banner


def disassemble(words: list[str], command: str) -> dict[str, str]:
    """Disassemble every word in one llvm-mc invocation, keyed by word."""
    # llvm-mc wants memory-order bytes, least significant first, each prefixed
    # with 0x.
    input_lines = []
    for word in sorted(words, key=lambda w: w.zfill(8)):
        padded = word.zfill(8)
        pairs = [padded[i : i + 2] for i in range(6, -1, -2)]
        input_lines.append(" ".join("0x" + p for p in pairs))
    result = subprocess.run(
        command,
        input="\n".join(input_lines),
        capture_output=True,
        text=True,
        shell=True,
    )
    if result.returncode != 0:
        sys.exit(f"{command} failed: {result.stderr.strip()}")

    decoded = {}
    for line in result.stdout.splitlines():
        match = ENCODING_RE.search(line)
        if not match:
            continue
        # Key on the echoed bytes, so a declined line cannot shift the answers
        # below it.
        echoed = BYTE_RE.findall(match.group(1))
        word = "".join(reversed(echoed)).lstrip("0")
        decoded[word] = line[: match.start()].replace("\t", " ").strip()
    if not decoded:
        sys.exit(
            "Nothing decoded. This needs a Morello build of llvm-mc; set"
            " CHERI_LLVM_MC or pass --llvm-mc."
        )
    return {w: decoded.get(w, NOT_DISASSEMBLED) for w in words}


def decode_in_place(path: str, command: str) -> None:
    """Replace each raw insn= word in PATH by "mnemonic (0x<word>)". Words that
    do not decode keep the form they arrived with, and the rewrite lands in one
    atomic rename so a failure leaves the log as it was."""
    try:
        with open(path) as log:
            text = log.read()
    except OSError as e:
        sys.exit(f"cannot rewrite {path}: {e}")
    found = list(RAW_INSN_RE.finditer(text))
    if not found:
        return
    decoded = disassemble(
        sorted({m.group(1).lstrip("0") for m in found}), command
    )
    annotated = 0

    def annotate(match: re.Match[str]) -> str:
        nonlocal annotated
        mnemonic = decoded[match.group(1).lstrip("0")]
        if mnemonic == NOT_DISASSEMBLED:
            return match.group(0)
        annotated += 1
        return f"insn={mnemonic} (0x{match.group(1)})"

    new_text = RAW_INSN_RE.sub(annotate, text)
    if not annotated:
        return
    fd, tmp_path = tempfile.mkstemp(
        dir=os.path.dirname(os.path.abspath(path)), prefix=".decode-insn-"
    )
    try:
        with os.fdopen(fd, "w") as tmp:
            tmp.write(new_text)
        os.chmod(tmp_path, os.stat(path).st_mode)
        os.replace(tmp_path, path)
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
    print(f"Annotated {annotated} of {len(found)} insn= lines in {path}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Decode insn= words from a CHERI fault dump."
    )
    parser.add_argument(
        "logs",
        nargs="*",
        default=["-"],
        help="Crash logs to scan, or - for stdin (default)",
    )
    parser.add_argument(
        "--llvm-mc",
        help="llvm-mc command, or its path (default: "
        "$CHERI_LLVM_MC, then llvm-mc on PATH)",
    )
    parser.add_argument(
        "--in-place",
        metavar="PATH",
        help="annotate the insn= words in PATH instead of "
        "printing a summary of the logs given",
    )
    args = parser.parse_args()

    llvm_mc = args.llvm_mc or os.environ.get("CHERI_LLVM_MC") or "llvm-mc"
    if args.in_place:
        decode_in_place(args.in_place, f"{llvm_mc} {LLVM_MC_FLAGS}")
        return

    counts: dict[str, int] = {}
    tests: dict[str, str] = {}
    for path in args.logs:
        if path == "-":
            text = sys.stdin.read()
        else:
            with open(path) as log:
                text = log.read()
        collect(counts, tests, text)

    if not counts:
        sys.exit("No insn= words found; is this a CHERI fault dump?")

    order = sorted(counts, key=lambda w: (-counts[w], w))
    decoded = disassemble(order, f"{llvm_mc} {LLVM_MC_FLAGS}")
    for word in order:
        print(
            f"{counts[word]:>5}  0x{word.zfill(8)}  {decoded[word]:<40}"
            f"  {tests.get(word, '')}"
        )
    print(
        f"\n{len(order)} distinct instructions, "
        f"{sum(counts.values())} faults."
    )


if __name__ == "__main__":
    main()
