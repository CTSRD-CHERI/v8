#!/usr/bin/env python3
# -
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 MSB Associates
#
# This software was developed by SRI International,
# Capabilities Limited, and MSB Associates under
# Defense Advanced Research Projects Agency (DARPA)
# Contract No. W912CG-26-C-A006
# ("Compartmentalised Chrome Demonstrator (CCD)")
# with the U.S. Army Contracting Command and the
# Defense Advanced Research Projects Agency.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

"""
Distributed V8 test runner.

This script discovers JavaScript and optional C++ tests in a V8 checkout,
splits them evenly across a list of machines (supporting CIDR and port ranges),
and runs them remotely via SSH. The test machines are assumed to be homogeneous
and any variation between them might cause test failures.
"""

from __future__ import annotations

import argparse
import ipaddress
import logging
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import (
    Any,
    Dict,
    Iterator,
    List,
    NoReturn,
    Optional,
    Pattern,
    Set,
    Tuple,
    Union,
)

DEFAULT_JS_SUITES: List[str] = [
    "mjsunit",
    "message",
    "intl",
    "debugger",
    "inspector",
    "wasm-js",
    "wasm-spec-tests",
    "test262",
    "webkit",
    "mozilla",
    "filecheck",
    "benchmarks",
]

# Without this, run-tests.py runs every variant.
DEFAULT_VARIANTS = "dev"

# For --help; --variants also takes bare variant names.
KNOWN_VARIANT_ALIASES = {
    "dev",
    "more",
    "exhaustive",
    "extra",
}

# Enumerated by running a binary on the target machine; 'bigint' has no .js.
DEFAULT_CPP_SUITES: List[str] = [
    "cctest",
    "unittests",
    "wasm-api-tests",
    "bigint",
]

FUZZING_SUITES: List[str] = ["fuzzer", "fuzzilli", "mkgrokdump", "js-perf-test"]
FUZZING_SUITE_INFO: Dict[str, Dict[str, Any]] = {
    "fuzzer": {"binary": None, "list": ["fuzzer"], "prefix": "fuzzer"},
}

JS_SUITE_INFO: Dict[str, Dict[str, Any]] = {
    "test262": {
        "path_replace": ("test262/data/test/", "test262/"),
    },
    "benchmarks": {
        "path_replace": ("benchmarks/data/", "benchmarks/"),
    },
    "wasm-spec-tests": {
        "path_replace": ("wasm-spec-tests/tests/", "wasm-spec-tests/"),
    },
}
CPP_SUITE_INFO: Dict[str, Dict[str, Any]] = {
    "cctest": {
        "binary": "./cctest",
        "list_arg": "--list",
        "parser": "cctest",
        "prefix": "cctest",
    },
    "unittests": {
        "binary": "./v8_unittests",
        "list_arg": "--gtest_list_tests",
        "parser": "gtest",
        "prefix": "unittests",
    },
    "wasm-api-tests": {
        "binary": "./wasm_api_tests",
        "list_arg": "--gtest_list_tests",
        "parser": "gtest",
        "prefix": "wasm-api-tests",
    },
    "bigint": {
        "binary": "./bigint_shell",
        "list_arg": "--list",
        "parser": "word-list",
        "prefix": "bigint",
    },
}

# Patterns to filter out from log files.
# FIXME: Brittle.
LOG_FILTER_PATTERNS: List[Pattern[str]] = [
    re.compile(r"^Build found: .*"),
    re.compile(r"^>>> Autodetected:.*"),
    re.compile(r"^DEBUG_defined,.*"),
    re.compile(r"^>>> Running tests for .*"),
    re.compile(r"^>>> Running with test processors"),
    re.compile(r"^>>> Statusfile variables:"),
]

# Directories to skip when walking JavaScript test suites.
DEFAULT_SKIP_DIRS: Set[str] = {
    "harness",
    "resources",
    "third_party",
    "lib",
    "implementation-contributed",
}

# What testsuite.JSTestLoader accepts.
JS_TEST_SUFFIXES: Tuple[str, ...] = (".js", ".mjs")

# Fallback exclusions, used only when a suite's testcfg.py cannot be read.
DEFAULT_EXCLUDE_IDS: Set[str] = {
    "mjsunit/mjsunit",
    "mjsunit/mjsunit_numfuzz",
    "mjsunit/utils",
    "message/message",
    "intl/overrides",
}


def warn(msg: str) -> None:
    logging.warning(msg)


def error(msg: str, exit_code: int = 1) -> NoReturn:
    logging.error(msg)
    sys.exit(exit_code)


def sanitize_machine_name(machine: str) -> str:
    return machine.replace(".", "_").replace(":", "_")


def _strip_js_suffix(name: str) -> str:
    for suffix in JS_TEST_SUFFIXES:
        name = name.removesuffix(suffix)
    return name


def read_excluded_files(v8_root: Path, suite: str) -> Optional[Set[str]]:
    """Read excluded_files out of a suite's testcfg.py, or None if absent."""
    testcfg = v8_root / "test" / suite / "testcfg.py"
    try:
        source = testcfg.read_text(encoding="utf-8")
    except OSError:
        return None

    match = re.search(
        r"^[ \t]*def[ \t]+excluded_files\b.*?return[ \t]+"
        r"(?P<expr>\{.*?\}|set\([^\n]*\)|[^\n]*)",
        source,
        re.DOTALL | re.MULTILINE,
    )
    if not match:
        return None

    # Entries may be module-level constants, so resolve those.
    consts = dict(
        re.findall(r"^([A-Z_][A-Z0-9_]*)\s*=\s*['\"]([^'\"]+)['\"]", source, re.M)
    )
    names: List[str] = []
    for token in re.findall(r"[A-Za-z_]\w*|'[^']*'|\"[^\"]*\"", match.group("expr")):
        if token[0] in "'\"":
            names.append(token[1:-1])
        elif token in consts:
            names.append(consts[token])
        else:
            return None  # unresolvable, so let the caller fall back
    return set(names)


def list_v8_javascript_test_ids(
    local_v8_root: Union[str, Path],
    suites: Optional[List[str]] = None,
    exclude_helpers: bool = True,
    extra_exclude_ids: Optional[Set[str]] = None,
    skip_dirs: Optional[Set[str]] = None,
) -> List[str]:
    """
    Recursively list JavaScript test files in a V8 checkout and convert them
    to test identifiers suitable for tools/run-tests.py.

    Args:
        local_v8_root: Path to local V8 source root.
        suites: List of JavaScript suite names to include (e.g., ['mjsunit', 'test262']).
                If None, all default suites are used.
        exclude_helpers: If True, drop files the suite's testcfg.py excludes,
                plus the built-in fallback list.
        extra_exclude_ids: Additional test identifiers to exclude.
        skip_dirs: Directory names to skip entirely.

    Returns:
        List of test identifiers (e.g., 'mjsunit/foo').
    """
    v8_root = Path(local_v8_root).resolve()
    if not v8_root.is_dir():
        raise FileNotFoundError(f"V8 root directory not found: {v8_root}")

    if suites is None:
        suites = DEFAULT_JS_SUITES

    if skip_dirs is None:
        skip_dirs = DEFAULT_SKIP_DIRS
    else:
        skip_dirs = set(skip_dirs)

    exclude_filenames: Set[str] = set(DEFAULT_EXCLUDE_IDS)
    if exclude_helpers:
        for suite in suites:
            declared = read_excluded_files(v8_root, suite)
            if declared is None:
                logging.debug(f"no excluded_files for '{suite}'; using fallback")
                continue
            # testcfg.py declares these with an extension; ids have none.
            exclude_filenames.update(
                f"{suite}/{name}" for name in map(_strip_js_suffix, declared)
            )
    if extra_exclude_ids:
        exclude_filenames.update(extra_exclude_ids)

    test_ids: List[str] = []
    tests_root = v8_root / "test"

    for suite in suites:
        suite_dir = tests_root / suite
        if not suite_dir.is_dir():
            warn(f"JavaScript suite '{suite}' not found, skipping.")
            continue

        if suite == "wasm-js":
            wasm_tests_dir = suite_dir / "tests"
            if not wasm_tests_dir.is_dir():
                warn(f"wasm-js tests directory not found: {wasm_tests_dir}, skipping.")
                continue
            for root, dirs, files in os.walk(wasm_tests_dir):
                dirs[:] = [d for d in dirs if d not in skip_dirs]
                for f in files:
                    if not f.endswith(".any.js"):
                        continue
                    full_path = Path(root) / f
                    rel_to_base = full_path.relative_to(wasm_tests_dir)
                    stem = str(rel_to_base)[: -len(".any.js")]
                    test_id = f"wasm-js/{stem}"
                    test_ids.append(test_id)
            continue

        # Generic handling for other JavaScript suites
        for root, dirs, files in os.walk(suite_dir):
            dirs[:] = [d for d in dirs if d not in skip_dirs]
            for f in files:
                if not f.endswith(JS_TEST_SUFFIXES):
                    continue
                full_path = Path(root) / f
                rel_path = full_path.relative_to(v8_root)
                test_id = _strip_js_suffix(
                    str(rel_path).replace("test/", "", 1)
                )

                if exclude_helpers and test_id in exclude_filenames:
                    continue

                # Apply suite-specific path adjustments if needed.
                if suite in JS_SUITE_INFO:
                    info = JS_SUITE_INFO[suite]
                    if "path_replace" in info:
                        old, new = info["path_replace"]
                        if test_id.startswith(old):
                            test_id = test_id.replace(old, new, 1)

                test_ids.append(test_id)

    return test_ids


def ssh_execute(
    machine: str,
    command: str,
    ssh_user: Optional[str] = None,
    input_data: Optional[str] = None,
    timeout: int = 60,
) -> Optional[subprocess.CompletedProcess]:
    """
    Execute a command on a remote machine via SSH.
    Returns a subprocess.CompletedProcess object, or None on failure/timeout.
    """
    if ssh_user:
        dest = f"{ssh_user}@{machine}"
    else:
        dest = machine
    # ConnectTimeout covers setup only, not the session, so long runs are fine.
    # accept-new trusts a host key on first use, which BatchMode needs for a
    # host not yet in known_hosts; it still refuses a changed key.
    ssh_cmd = [
        "ssh",
        "-o",
        "ConnectTimeout=20",
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=accept-new",
        dest,
        command,
    ]
    try:
        result = subprocess.run(
            ssh_cmd,
            input=input_data,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return result
    except subprocess.TimeoutExpired as e:
        warn(f"SSH timeout on {machine} after {timeout}s: {e}")
        return None
    except Exception as e:
        warn(f"SSH error on {machine}: {e}")
        return None


def parse_cctest_list(output: str) -> List[str]:
    """
    Parse output of 'cctest --list'.
    Lines look like: "**>Test: test-weak-references/ObjectWithWeakReferencePromoted"
    """
    tests: List[str] = []
    total: Optional[int] = None
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("**>Test:"):
            test_name = line[len("**>Test:") :].strip()
            if test_name:
                tests.append(f"cctest/{test_name}")
        elif line.startswith("Total number of tests:"):
            try:
                total = int(line[len("Total number of tests:") :].strip())
            except ValueError:
                warn(f"Could not parse cctest test total from: {line!r}")

    if total is None:
        warn(f"cctest --list gave no test count; cannot check {len(tests)} tests")
    elif total != len(tests):
        error(
            f"cctest --list reported {total} tests but parsed {len(tests)}",
            exit_code=2,
        )
    return tests


def parse_word_list(output: str, prefix: str) -> List[str]:
    """Parse a shell that lists one bare test name per word."""
    return [f"{prefix}/{name}" for name in output.split() if name]


def parse_gtest_list(output: str, prefix: str) -> List[str]:
    """
    Parse output of any gtest --gtest_list_tests command.
    Returns a list of test identifiers like 'prefix/case.name'.
    """
    tests: List[str] = []
    current_case: Optional[str] = None
    for line in output.splitlines():
        line = line.rstrip()
        if not line:
            continue
        # A test case line ends with a dot and has no leading space
        if not line.startswith(" ") and line.endswith("."):
            current_case = line[:-1]
        elif line.startswith("  ") and current_case:
            test_name = line.strip()
            tests.append(f"{prefix}/{current_case}.{test_name}")
    return tests


def get_cpp_tests_from_machine(
    machine: str,
    remote_v8_root: str,
    build_dir: str,
    ssh_user: Optional[str] = None,
    suites: Optional[List[str]] = None,
) -> List[str]:
    """List C++ tests on a remote machine."""
    if suites is None:
        suites = DEFAULT_CPP_SUITES

    cpp_tests: List[str] = []
    for suite in suites:
        info = CPP_SUITE_INFO.get(suite) or FUZZING_SUITE_INFO.get(suite)
        if not info:
            warn(f"Unknown C++ suite '{suite}', skipping.")
            continue

        # Special case: suites without a binary (like fuzzer)
        if info["binary"] is None:
            tests = info.get("list", [])
            # Apply prefix if needed (fuzzer already is just "fuzzer")
            prefixed = [
                f"{info['prefix']}/{t}" if not t.startswith(info["prefix"]) else t
                for t in tests
            ]
            cpp_tests.extend(prefixed)
            logging.info(f"Added {len(prefixed)} {suite} tests.")
            continue

        # Build command to list tests
        cmd = f"cd {remote_v8_root} && cd {build_dir} && {info['binary']} {info['list_arg']}"
        result = ssh_execute(machine, cmd, ssh_user, timeout=60)
        if result is None or result.returncode != 0:
            warn(f"Could not retrieve {suite} list from {machine}")
            continue

        # Parse output according to the parser type
        if info["parser"] == "cctest":
            tests = parse_cctest_list(result.stdout)
        elif info["parser"] == "gtest":
            tests = parse_gtest_list(result.stdout, info["prefix"])
        elif info["parser"] == "word-list":
            tests = parse_word_list(result.stdout, info["prefix"])
        else:
            warn(f"Unknown parser '{info['parser']}' for suite {suite}")
            continue

        cpp_tests.extend(tests)
        logging.info(f"Discovered {len(tests)} {suite} tests.")

    return cpp_tests


def expand_machine_pattern(line: str) -> Iterator[str]:
    """
    Expand a line from the machine list file.
    - If the line contains both CIDR and a port range (e.g., '192.168.1.0/24:[2222-2222]'),
      it is rejected with an error.
    - If it contains '/' (CIDR), generate all host addresses in the subnet.
    - If it contains a port range like 'host:[start-end]', expand to all ports.
    - Otherwise, yield the line as a single machine.
    """
    line = line.strip()
    if not line:
        return

    port_range_pattern = r"^(.*?):\[(\d+)-(\d+)\]$"

    # Check for invalid combination of CIDR and port range.
    if "/" in line and re.match(port_range_pattern, line):
        error(
            f"Invalid machine specification: '{line}' contains both CIDR and port range. These cannot be combined."
        )

    # 1. CIDR expansion
    if "/" in line:
        try:
            network = ipaddress.ip_network(line, strict=False)
            hosts = list(network.hosts())
            if not hosts:
                warn(f"No hosts in network {line}")
            for ip in hosts:
                yield str(ip)
        except Exception as e:
            warn(f"Error parsing CIDR '{line}': {e}")
            yield line  # fallback to literal
        return

    # 2. Port range expansion: host:[start-end]
    match = re.match(port_range_pattern, line)
    if match:
        host = match.group(1)
        start_port = int(match.group(2))
        end_port = int(match.group(3))
        if start_port > end_port:
            warn(f"Invalid port range '{line}' (start > end). Treating as literal.")
            yield line
            return
        if end_port - start_port > 1000:
            warn(
                f"Port range '{line}' expands to {end_port - start_port + 1} ports. This may be large."
            )
        for port in range(start_port, end_port + 1):
            yield f"{host}:{port}"
        return

    # 3. Literal host
    yield line


def get_machine_list(filepath: str) -> List[str]:
    """
    Parse the machine list file, expand CIDR notations and port ranges,
    and return a flat list of unique machine addresses.
    """
    machines: List[str] = []
    seen: Set[str] = set()
    try:
        with open(filepath, "r") as f:
            for raw_line in f:
                line = raw_line.strip()
                if not line or line[0] == "#":
                    continue
                for expanded in expand_machine_pattern(line):
                    if expanded not in seen:
                        seen.add(expanded)
                        machines.append(expanded)
        return machines
    except FileNotFoundError:
        error(f"Machine list file '{filepath}' not found.")
    except Exception as e:
        error(f"Error reading machine list file: {e}")
    return []  # unreachable, but for type checker


def split_tests_evenly(tests: List[str], machines: List[str]) -> Dict[str, List[str]]:
    """Split tests into as even as possible chunks, one per machine."""
    num_machines = len(machines)
    num_tests = len(tests)
    chunk_size = num_tests // num_machines
    remainder = num_tests % num_machines

    assignments: Dict[str, List[str]] = {}
    start = 0
    for i, machine in enumerate(machines):
        end = start + chunk_size + (1 if i < remainder else 0)
        assignments[machine] = tests[start:end]
        start = end
    return assignments


def run_tests_on_machine(
    machine: str,
    test_list: List[str],
    remote_v8_root: str,
    build_dir: str,
    ssh_user: Optional[str] = None,
    log_dir: Optional[str] = None,
    batch_size: int = 50,
    test_timeout: int = 60,
    variants: str = DEFAULT_VARIANTS,
) -> Tuple[str, bool, str]:
    """
    SSH into a single machine and run all its assigned tests using xargs with batches.
    `build_dir` is relative to `remote_v8_root`.
    Returns (machine, overall_success, combined_output).
    """
    if not test_list:
        logging.info(f"[{machine}] No tests assigned, skipping.")
        return machine, True, ""

    # Prepare the test list as a string with newline separation (for stdin)
    test_data = "\n".join(test_list)

    # Build the remote command that reads the test list from stdin.
    remote_cmd = f"""
set -e
cd "{shlex.quote(remote_v8_root)}"
xargs -n {batch_size} tools/run-tests.py -p verbose -t {test_timeout} \
  --variants={variants} --exit-after-n-failures=0 \
  --outdir="{shlex.quote(build_dir)}"
"""

    # Prepare log file if requested
    log_path: Optional[Path] = None
    if log_dir:
        log_dir_path = Path(log_dir)
        log_dir_path.mkdir(exist_ok=True)
        safe = sanitize_machine_name(machine)
        log_path = log_dir_path / f"{safe}.log"

    logging.info(
        f"[{machine}] Starting test run ({len(test_list)} tests, batch size {batch_size})..."
    )

    start_time = time.time()
    result = ssh_execute(
        machine, remote_cmd, ssh_user, input_data=test_data, timeout=21600
    )  # 6 hours
    elapsed = time.time() - start_time

    if result is None:
        # Still write the log, so a stale one can't be read as this run's.
        if log_path:
            try:
                with open(log_path, "w") as f:
                    f.write("=== SSH connection failed\n")
            except OSError as e:
                warn(f"Could not write log for {machine}: {e}")
        return machine, False, "SSH connection failed"

    output = result.stdout + result.stderr
    success = result.returncode == 0

    if log_path:
        try:
            with open(log_path, "w") as f:
                f.write(output)
            logging.info(
                f"[{machine}] Finished in {elapsed:.1f}s, exit code {result.returncode}, "
                f"log saved to {log_path}"
            )
        except Exception as e:
            warn(f"Could not write log for {machine}: {e}")
            logging.info(
                f"[{machine}] Finished in {elapsed:.1f}s, exit code {result.returncode}"
            )
    else:
        logging.info(
            f"[{machine}] Finished in {elapsed:.1f}s, exit code {result.returncode}"
        )

    return machine, success, output


def run_tests_on_machines(
    assignments: Dict[str, List[str]],
    remote_v8_root: str,
    build_dir: str,
    ssh_user: Optional[str] = None,
    log_dir: Optional[str] = None,
    parallel: bool = True,
    max_workers: int = 10,
    batch_size: int = 50,
    test_timeout: int = 60,
    variants: str = DEFAULT_VARIANTS,
) -> Dict[str, Tuple[bool, str]]:
    """
    Run tests on all machines, either sequentially or in parallel.
    Returns a dict mapping machine -> (success, output).
    """
    machines = list(assignments.keys())
    results: Dict[str, Tuple[bool, str]] = {}

    if parallel:
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_to_machine = {
                executor.submit(
                    run_tests_on_machine,
                    machine,
                    assignments[machine],
                    remote_v8_root,
                    build_dir,
                    ssh_user,
                    log_dir,
                    batch_size,
                    test_timeout,
                    variants,
                ): machine
                for machine in machines
            }
            for future in as_completed(future_to_machine):
                machine, success, output = future.result()
                results[machine] = (success, output)
    else:
        for machine in machines:
            machine, success, output = run_tests_on_machine(
                machine,
                assignments[machine],
                remote_v8_root,
                build_dir,
                ssh_user,
                log_dir,
                batch_size,
                test_timeout,
                variants,
            )
            results[machine] = (success, output)

    logging.info("=" * 60)
    logging.info("Test run summary:")
    success_count = sum(1 for s, _ in results.values() if s)
    for machine, (success, _) in results.items():
        status = "SUCCESS" if success else "FAILURE"
        if success:
            logging.info(f"  {machine}: {status}")
        else:
            # Warn, so failures show without -v.
            warn(f"  {machine}: {status}")
    logging.info(f"Overall: {success_count}/{len(machines)} machines succeeded.")
    return results


def is_filtered_line(line: str) -> bool:
    return any(p.match(line) for p in LOG_FILTER_PATTERNS)


def parse_status_line(line: str) -> Optional[str]:
    """
    If line is a test status line, return the line (or a tuple) for collection.
    Format: "<test> <variant>: <STATUS>"
    """
    status_re = re.compile(
        r"^\S.*?(?:\s+\S+)?:\s+(PASS|FAIL|TIMEOUT|CRASH|SKIP)$"
    )
    return line if status_re.match(line) else None


def parse_summary_line(line: str) -> Optional[Tuple[str, Union[int, bool]]]:
    """
    Check for summary lines and return a tuple indicating the type and value.
    Returns:
        ('ran', count) for ">>> N tests ran"
        ('failed', count) for "=== N tests failed"
        ('all_succeeded', True) for "=== All tests succeeded"
        None otherwise.
    """
    failed_summary_re = re.compile(r"^=== (\d+) tests failed$")
    all_succeeded_re = re.compile(r"^=== All tests succeeded$")
    tests_ran_re = re.compile(r"^>>> (\d+) tests ran$")

    m = failed_summary_re.match(line)
    if m:
        return ("failed", int(m.group(1)))
    m = all_succeeded_re.match(line)
    if m:
        return ("all_succeeded", True)
    m = tests_ran_re.match(line)
    if m:
        return ("ran", int(m.group(1)))
    return None


def extract_error_blocks(lines: List[str]) -> Iterator[str]:
    """
    Generator that yields error blocks (multiline strings) from a list of lines.
    An error block starts with a line "=== " and continues until the next line
    starting with "=== " or EOF. Filtered lines inside the block are skipped.
    """
    i = 0
    n = len(lines)
    while i < n:
        raw_line = lines[i].rstrip("\n")
        i += 1
        if is_filtered_line(raw_line):
            continue
        # Check if this line starts a block (and is not a summary line we already handled)
        if raw_line.startswith("=== ") and not parse_summary_line(raw_line):
            block: List[str] = [raw_line]
            # Collect subsequent lines until next "=== " or EOF
            while i < n:
                next_line = lines[i].rstrip("\n")
                if next_line.startswith("=== "):
                    break
                if not is_filtered_line(next_line):
                    block.append(next_line)
                i += 1
            yield "\n".join(block)
        # else: ignore line (not start of block)


def combine_logs(log_dir: str, output_file: str) -> None:
    """
    Read all .log files in `log_dir`, filter out lines matching LOG_FILTER_PATTERNS,
    then reorganize the output:
      1. All single‑line test status results
      2. All detailed error blocks
      3. A final summary with total tests ran and total tests failed.
    """
    log_dir_path = Path(log_dir)
    if not log_dir_path.is_dir():
        error(f"Log directory '{log_dir}' does not exist.")

    log_files = sorted(log_dir_path.glob("*.log"))
    if not log_files:
        warn(f"No .log files found in '{log_dir}'.")
        return

    status_lines: List[str] = []
    error_blocks: List[str] = []
    total_ran: int = 0
    total_failed: int = 0

    for log_file in log_files:
        try:
            with open(log_file, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            warn(f"Error reading {log_file}: {e}")
            continue

        # Collect status lines, error blocks, and summary counts
        i = 0
        n = len(lines)
        while i < n:
            raw_line = lines[i].rstrip("\n")
            i += 1

            # Skip filtered lines
            if is_filtered_line(raw_line):
                continue

            # Check for status line
            status = parse_status_line(raw_line)
            if status:
                status_lines.append(status)
                continue

            # Check for summary line
            summary = parse_summary_line(raw_line)
            if summary:
                kind, value = summary
                if kind == "ran":
                    total_ran += value  # type: ignore[operator]
                elif kind == "failed":
                    total_failed += value  # type: ignore[operator]
                # 'all_succeeded' adds nothing
                continue

            # Check if this line starts an error block
            if raw_line.startswith("=== ") and not parse_summary_line(raw_line):
                block_lines = [raw_line]
                # Collect subsequent lines until next "=== " or EOF
                while i < n:
                    next_line = lines[i].rstrip("\n")
                    if next_line.startswith("=== "):
                        break
                    if not is_filtered_line(next_line):
                        block_lines.append(next_line)
                    i += 1
                error_blocks.append("\n".join(block_lines))

    # Write combined output
    output_path = Path(output_file)
    with open(output_path, "w", encoding="utf-8") as out_f:
        for line in sorted(status_lines):
            out_f.write(line + "\n")
        if status_lines and error_blocks:
            out_f.write("\n")
        for block in error_blocks:
            out_f.write(block + "\n")

        out_f.write("\n")
        if total_ran == 0:
            # 0 ran means nothing ran, not that everything passed.
            out_f.write("=== No tests ran\n")
        elif total_failed == 0:
            out_f.write("=== All tests succeeded\n")
        else:
            out_f.write(f"=== {total_failed} tests failed\n")
        out_f.write(f">>> {total_ran} tests ran\n")

    logging.info(
        f"Combined {len(log_files)} log files into '{output_file}' "
        f"(status lines: {len(status_lines)}, error blocks: {len(error_blocks)})."
    )


def detect_build_config(
    machine: str, remote_v8_root: str, build_dir: str, ssh_user: Optional[str]
) -> Dict[str, str]:
    """Ask run-tests.py to describe the build it would use."""
    cmd = (
        f"cd {remote_v8_root} && "
        f"tools/run-tests.py --outdir={build_dir} "
        f"no_such_suite/NoSuchTest 2>&1 | head -5"
    )
    result = ssh_execute(machine, cmd, ssh_user, timeout=300)
    if result is None:
        warn("Could not query build configuration; assuming defaults.")
        return {}

    config: Dict[str, str] = {}
    for line in (result.stdout + result.stderr).splitlines():
        if "=" not in line:
            continue
        for pair in line.split(","):
            if "=" not in pair:
                continue
            key, _, value = pair.partition("=")
            key = key.strip()
            if re.fullmatch(r"[A-Za-z0-9_]+", key):
                config[key] = value.strip()
    return config


def log_build_config(config: Dict[str, str]) -> None:
    for key in ["is_cheri", "i18n", "dcheck_always_on", "slow_dchecks", "mode"]:
        if key in config:
            logging.info(f"  {key}={config[key]}")


# run-tests.py's default.
DEFAULT_TEST_TIMEOUT = 60
SLOW_BUILD_TIMEOUT_FACTOR = 4


def compute_test_timeout(
    config: Dict[str, str], base_timeout: int, is_default: bool
) -> int:
    """Scale up the default timeout on dcheck builds."""
    if not is_default:
        return base_timeout
    slow = config.get("dcheck_always_on", "").lower() == "true" or config.get(
        "slow_dchecks", ""
    ).lower() == "true"
    if not slow:
        return base_timeout
    timeout = base_timeout * SLOW_BUILD_TIMEOUT_FACTOR
    logging.info(f"dcheck build: timeout {base_timeout}s -> {timeout}s")
    return timeout


def default_suite_dirs(v8_root: Union[str, Path]) -> List[str]:
    """Return the test/ subdirectories containing a testcfg.py."""
    tests_root = Path(v8_root) / "test"
    if not tests_root.is_dir():
        return []
    return sorted(
        d.name
        for d in tests_root.iterdir()
        if d.is_dir() and (d / "testcfg.py").is_file()
    )


def filter_suites(requested: Optional[List[str]], allowed_list: List[str]) -> List[str]:
    """
    Return the list of suites to include.
    If requested is None, return allowed_list.
    Otherwise, return the intersection of requested and allowed_list.
    """
    if requested is None:
        return allowed_list
    return [s for s in requested if s in allowed_list]


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Split V8 tests evenly across a list of machines (supports CIDR and port ranges) and optionally run them."
    )
    parser.add_argument(
        "-M",
        "--machine-list",
        default=os.path.expanduser("~/.v8-test-machines"),
        help="Path to a file containing a list of machines (one per line). "
        "Lines can be IPs/hostnames, CIDR (e.g., 192.168.1.0/24), "
        "or port ranges (e.g., 192.168.1.141:[2222-3333], localhost:[2222-2322]). "
        "Default: $HOME/.v8-test-machines",
    )
    parser.add_argument(
        "-d",
        "--output-directory",
        default=os.getcwd(),
        help="Directory to store logs. Default: current directory.",
    )
    parser.add_argument(
        "-b",
        "--build-directory",
        default="./out/release",
        help="Path to the V8 build directory **relative to the remote V8 root**. "
        "Default: ./out/release",
    )
    parser.add_argument(
        "--v8-root",
        "--local-v8-root",
        dest="local_v8_root",
        default=".",
        help="Path to the local V8 source root (for test discovery). Default: current directory.",
    )
    parser.add_argument(
        "--remote-v8-root",
        default=None,
        help="Path to the V8 source root on remote machines. If not provided, defaults to the local --v8-root value.",
    )
    parser.add_argument(
        "--suites",
        default=None,
        help="Comma-separated list of test suites to include (e.g., 'mjsunit,cctest,unittests'). "
        "If not provided, all suites are included.",
    )
    # Remote execution arguments
    parser.add_argument(
        "--ssh-user",
        default=None,
        help="Optional SSH username to prepend to machine addresses (e.g., 'ubuntu').",
    )
    parser.add_argument(
        "--log-dir",
        default=None,
        help="Directory to store per-machine log files (stdout/stderr). If not set, logs are printed to console.",
    )
    parser.add_argument(
        "--parallel",
        action="store_true",
        default=True,
        help="Run tests on machines in parallel (default).",
    )
    parser.add_argument(
        "--sequential",
        action="store_false",
        dest="parallel",
        help="Run tests on machines sequentially (one after another).",
    )
    parser.add_argument(
        "--max-workers",
        type=int,
        default=10,
        help="Maximum number of parallel SSH connections when --parallel is used. Default: 10.",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=50,
        help="Number of tests to pass to each tools/run-tests.py invocation via xargs. "
        "Smaller values reduce command-line length but increase overhead. Default: 50.",
    )
    parser.add_argument(
        "--variants",
        default=DEFAULT_VARIANTS,
        help="Passed to tools/run-tests.py --variants: a comma-separated "
        "list, or one of "
        f"{', '.join(sorted(KNOWN_VARIANT_ALIASES))}. "
        f"Default {DEFAULT_VARIANTS!r}.",
    )
    parser.add_argument(
        "--test-timeout",
        type=int,
        default=None,
        help="Per-test timeout in seconds passed to tools/run-tests.py -t. "
        f"Default: {DEFAULT_TEST_TIMEOUT}, scaled up automatically on "
        "dcheck/slow-dcheck builds. An explicit value is used as given.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Discover and split tests, print the plan and the exact per-machine "
        "invocation, then exit without running anything.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase verbosity (use -v for INFO, -vv for DEBUG).",
    )

    args = parser.parse_args()

    for name in ("max_workers", "batch_size", "test_timeout"):
        value = getattr(args, name)
        if value is not None and value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be greater than 0")

    # Configure logging
    log_level = logging.WARNING
    if args.verbose == 1:
        log_level = logging.INFO
    elif args.verbose >= 2:
        log_level = logging.DEBUG
    logging.basicConfig(level=log_level, format="%(levelname)s: %(message)s")
    if args.dry_run:
        # Show the plan even without -v.
        logging.getLogger().setLevel(logging.INFO)

    # Get expanded machine list
    machines = get_machine_list(args.machine_list)
    if not machines:
        error("Machine list is empty after expansion.")

    logging.info(f"Expanded machine list to {len(machines)} entries.")

    # A suite with no testcfg.py selects nothing, so drop it.
    available = set(default_suite_dirs(args.local_v8_root))
    if available:
        logging.info("Suites with a testcfg.py: %s", ", ".join(sorted(available)))

    # Fuzzing suites stay requestable but are not defaults.
    fuzzing = set(FUZZING_SUITES)
    if args.suites is None:
        allowed_js = [s for s in DEFAULT_JS_SUITES if s not in fuzzing]
        allowed_cpp = [s for s in DEFAULT_CPP_SUITES if s not in fuzzing]
    else:
        allowed_js = DEFAULT_JS_SUITES + FUZZING_SUITES
        allowed_cpp = DEFAULT_CPP_SUITES + FUZZING_SUITES

    requested_suites: Optional[List[str]] = None
    if args.suites:
        requested_suites = [s.strip() for s in args.suites.split(",") if s.strip()]
        known_suites = set(allowed_js + allowed_cpp)
        for s in requested_suites:
            if s not in known_suites:
                warn(f"Unknown suite '{s}' requested. It will be ignored.")
            elif available and s not in available:
                warn(f"Suite '{s}' has no test/<dir>/testcfg.py in this checkout.")
        if any(s in fuzzing for s in requested_suites):
            warn("Fuzzing suites requested; these can run very long.")
    else:
        requested_suites = None  # Signals "use defaults"

    # Collect tests
    all_tests: List[str] = []

    # JavaScript suites
    js_suites = filter_suites(requested_suites, allowed_js)
    if js_suites:
        try:
            js_tests = list_v8_javascript_test_ids(args.local_v8_root, suites=js_suites)
            all_tests.extend(js_tests)
            logging.info(f"Found {len(js_tests)} JavaScript test files.")
        except FileNotFoundError as e:
            error(str(e))

    # Determine remote V8 root (fallback to local)
    remote_v8_root = (
        args.remote_v8_root if args.remote_v8_root is not None else args.local_v8_root
    )

    # C++ suites
    cpp_suites = filter_suites(requested_suites, allowed_cpp)
    if cpp_suites:
        if not machines:
            warn("No machines available to run C++ test discovery. Skipping C++ tests.")
        else:
            logging.info(
                f"Connecting to first machine ({machines[0]}) to discover C++ tests for suites: {cpp_suites}..."
            )
            cpp_tests = get_cpp_tests_from_machine(
                machines[0],
                remote_v8_root,
                args.build_directory,
                ssh_user=args.ssh_user,
                suites=cpp_suites,
            )
            if cpp_tests:
                all_tests.extend(cpp_tests)
                logging.info(
                    f"Added {len(cpp_tests)} C++ tests. Total now: {len(all_tests)}"
                )
            else:
                warn("No C++ tests discovered or SSH failed.")

    if not all_tests:
        logging.info("No tests found. Exiting.")
        sys.exit(0)

    # Split tests evenly across all machines
    assignments = split_tests_evenly(all_tests, machines)

    logging.info(f"Test assignments for {len(machines)} machines:")
    for machine, test_list in assignments.items():
        logging.info(f"  {machine}: {len(test_list)} tests")

    log_dir = args.log_dir if args.log_dir else args.output_directory

    build_config: Dict[str, str] = {}
    if machines:
        logging.info("Querying build configuration...")
        build_config = detect_build_config(
            machines[0], remote_v8_root, args.build_directory, args.ssh_user
        )
        log_build_config(build_config)
    test_timeout = compute_test_timeout(
        build_config,
        DEFAULT_TEST_TIMEOUT if args.test_timeout is None else args.test_timeout,
        is_default=args.test_timeout is None,
    )

    if args.dry_run:
        logging.info("=" * 60)
        logging.info("Dry run, nothing will be executed.")
        logging.info(f"  suites:   JS={js_suites} C++={cpp_suites}")
        logging.info(f"  tests:    {len(all_tests)} total")
        logging.info(f"  machines: {len(machines)}")
        logging.info(f"  batch:    {args.batch_size}")
        logging.info(f"  timeout:  {test_timeout}s per test")
        logging.info(f"  variants: {args.variants}")
        logging.info(f"  outdir:   {args.build_directory}")
        logging.info(f"  v8 root:  {remote_v8_root}")
        for machine, test_list in assignments.items():
            logging.info(f"  {machine}: {len(test_list)} tests")
        logging.info("Representative invocation:")
        logging.info(
            f"  ssh {machines[0]} 'cd {remote_v8_root} && "
            f"xargs -n {args.batch_size} tools/run-tests.py -p verbose "
            f"-t {test_timeout} --variants={args.variants} "
            f"--exit-after-n-failures=0 "
            f"--outdir={args.build_directory}'"
        )
        for test_id in sorted(all_tests)[:5]:
            logging.info(f"  {test_id}")
        return

    logging.info(f"Starting remote test execution on {len(machines)} machines...")

    # Don't combine a previous run's logs with this one's.
    stale = [
        p
        for p in Path(log_dir).glob("*.log")
        if p.name != "combined_results.log"
    ]
    for p in stale:
        try:
            p.unlink()
        except OSError as e:
            warn(f"Could not remove stale log {p}: {e}")
    if stale:
        logging.info(f"Removed {len(stale)} log file(s) from a previous run.")

    run_tests_on_machines(
        assignments,
        remote_v8_root=remote_v8_root,
        build_dir=args.build_directory,
        ssh_user=args.ssh_user,
        log_dir=log_dir,
        parallel=args.parallel,
        max_workers=args.max_workers,
        batch_size=args.batch_size,
        test_timeout=test_timeout,
        variants=args.variants,
    )

    combined_file = os.path.join(log_dir, "combined_results.log")

    # Rename existing combined log file if present (keep one backup)
    if os.path.exists(combined_file):
        old_combined = combined_file + ".old"
        # Remove previous .old to avoid accumulation (overwrite)
        if os.path.exists(old_combined):
            try:
                os.remove(old_combined)
                logging.debug(f"Removed previous {old_combined}")
            except OSError as e:
                warn(f"Could not remove {old_combined}: {e}")
        try:
            os.rename(combined_file, old_combined)
            logging.info(f"Renamed existing {combined_file} to {old_combined}")
        except OSError as e:
            warn(f"Could not rename {combined_file} to {old_combined}: {e}")

    logging.info(f"Combining logs from '{log_dir}' into '{combined_file}'...")
    combine_logs(log_dir, combined_file)


if __name__ == "__main__":
    main()
