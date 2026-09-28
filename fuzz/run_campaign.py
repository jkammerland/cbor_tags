#!/usr/bin/env python3
"""Run each registered property with a budget, persistent corpus and evidence."""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def tool_info(name):
    path = shutil.which(name)
    if not path:
        return {"available": False}
    resolved = pathlib.Path(path).resolve()
    result = subprocess.run(
        [path, "--version"], capture_output=True, text=True, check=True
    )
    return {
        "invoked_path": path,
        "resolved_path": str(resolved),
        "version": (result.stdout or result.stderr).strip(),
        "sha256": sha256(resolved),
    }


def run_process(command, env, log, timeout):
    # Terminate the whole process group on timeout, including any subprocesses.
    with subprocess.Popen(
        command,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
        start_new_session=True,
    ) as process:
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            return 124


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--seconds", type=int, default=60, help="Budget per property")
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
        help="New directory for this campaign's evidence",
    )
    parser.add_argument(
        "--corpus", type=pathlib.Path, required=True, help="Reusable corpus directory"
    )
    parser.add_argument("--filter", default="", help="Substring of property name")
    parser.add_argument(
        "--jobs", type=int, default=1, help="Independent fuzzing processes"
    )
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    if args.jobs <= 0:
        parser.error("--jobs must be positive")
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    corpus = args.corpus.resolve()
    corpus.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    env["LLVM_PROFILE_FILE"] = str(output / "coverage-%p.profraw")
    listing = subprocess.check_output(
        [str(binary), "--list_fuzz_tests=1"], text=True, env=env
    )
    tests = re.findall(r"^(?:\[\*\] )?Fuzz test: (\S+)$", listing, re.MULTILINE)
    tests = [test for test in tests if args.filter in test]
    if not tests:
        raise RuntimeError(f"No properties selected; FuzzTest listing was: {listing!r}")
    root = pathlib.Path(__file__).resolve().parent.parent
    report = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "binary": str(binary),
        "binary_sha256": sha256(binary),
        "seconds_per_property": args.seconds,
        "jobs": args.jobs,
        "asan_options": env["ASAN_OPTIONS"],
        "tools": {
            name: tool_info(name)
            for name in ("clang++", "cmake", "ninja", "llvm-cov", "llvm-profdata")
        },
        "results": [],
    }
    cache = binary.parent / "CMakeCache.txt"
    if cache.exists():
        shutil.copyfile(cache, output / cache.name)
        for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"):
            match = re.search(rf"^{key}:[^=]+=(.+)$", cache.read_text(), re.MULTILINE)
            if match:
                report["tools"][key] = tool_info(match[1])
        dependencies = list((binary.parent / "_deps").glob("*-src"))
        match = re.search(
            r"^fuzztest_SOURCE_DIR:[^=]+=(.+)$", cache.read_text(), re.MULTILINE
        )
        if match:
            dependencies.append(pathlib.Path(match[1]))
        report["dependency_revisions"] = {}
        for dependency in dependencies:
            if (dependency / ".git").exists():
                report["dependency_revisions"][str(dependency)] = (
                    subprocess.check_output(
                        ["git", "-C", str(dependency), "rev-parse", "HEAD"], text=True
                    ).strip()
                )
    commands = binary.parent / "compile_commands.json"
    if commands.exists():
        shutil.copyfile(commands, output / commands.name)
    report["source_commit"] = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    sources = subprocess.check_output(
        [
            "git",
            "-C",
            str(root),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
            "fuzz",
            "include",
            "cbor_tags_config.h.in",
        ],
        text=True,
    ).split("\0")
    report["source_sha256"] = {
        name: sha256(root / name)
        for name in sources
        if name and (root / name).is_file()
    }

    def run_one(test):
        test_env = env.copy()
        test_corpus = corpus / test
        test_corpus.mkdir(exist_ok=True)
        test_env["FUZZTEST_TESTSUITE_IN_DIR"] = str(test_corpus)
        test_env["FUZZTEST_TESTSUITE_OUT_DIR"] = str(test_corpus)
        command = [
            str(binary),
            f"--fuzz={test}",
            f"--fuzz_for={args.seconds}s",
            "--corpus_database=",
        ]
        started = time.monotonic()
        log_path = output / f"{test}.log"
        with log_path.open("w") as log:
            code = run_process(command, test_env, log, timeout=args.seconds + 60)
        return {
            "test": test,
            "command": command,
            "returncode": code,
            "elapsed_seconds": time.monotonic() - started,
            "log": log_path.name,
        }

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(run_one, test) for test in tests]
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            report["results"].append(result)
            (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
            outcome = "PASS" if result["returncode"] == 0 else "FAIL"
            print(f"{result['test']}: {outcome} ({result['returncode']})", flush=True)
    return int(any(result["returncode"] for result in report["results"]))


if __name__ == "__main__":
    sys.exit(main())
