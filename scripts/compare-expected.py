#!/usr/bin/env python3
"""Compare expected backends without changing the production backend selector."""

import argparse
import csv
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "benchmarks" / "expected_compare"
BACKENDS = ("ours", "tl")
OPTIMIZATIONS = ("O2", "O3", "Os")


def command(args, log=None):
    result = subprocess.run(
        [str(arg) for arg in args],
        text=True,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if log:
        Path(log).write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n{result.stdout}"
        )
    return result.stdout


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sections(path):
    entries = {}
    for line in command(["size", "-A", path]).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith("."):
            entries[fields[0]] = int(fields[1])
    return entries


def symbols(path):
    result = {}
    for line in command(
        ["nm", "-S", "--defined-only", "--format=posix", path]
    ).splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[1] in "TtWw":
            result[fields[0]] = int(fields[3], 16)
    return result


def footprint(entries):
    return {
        "code": sum(
            n
            for name, n in entries.items()
            if name.startswith((".text", ".expected_cmp"))
        ),
        "rodata": sum(n for name, n in entries.items() if name.startswith(".rodata")),
        "unwind": sum(
            n
            for name, n in entries.items()
            if name.startswith((".eh_frame", ".gcc_except_table"))
        ),
    }


def compiler_label(path):
    return "clang" if "clang" in Path(path).name else "gcc"


def write_report(output, builds, runtimes, layouts, meta):
    lines = [
        "cbor_tags expected comparison",
        "",
        f"Source commit: {meta['source_commit']}",
        f"tl::expected header: {meta['tl_include']} (sha256 {meta['tl_header_sha256']})",
        "",
        "Both backends use the same C++20 source and the 0.26.0 codec headers. A generated include override selects tl for the comparison only. The comparison does not add a public backend option.",
        "",
        "Object layout (ours / tl; bytes)",
    ]
    for compiler, data in layouts.items():
        a, b = data["ours"], data["tl"]
        lines.append(compiler)
        for name in a:
            lines.append(
                f"  {name}: size {a[name]['sizeof']}/{b[name]['sizeof']}, alignment {a[name]['alignof']}/{b[name]['alignof']}; trivial copy assignment {a[name]['trivial_copy_assign']}/{b[name]['trivial_copy_assign']}"
            )
    lines += [
        "",
        "Code footprint: ours / tl, change",
        "compiler | optimization | kernel object code | linked code | linked rodata | linked unwind",
    ]
    for compiler in layouts:
        for optimization in OPTIMIZATIONS:
            a, b = (
                builds[f"{compiler}-{optimization}-{backend}"] for backend in BACKENDS
            )
            values = []
            for container, key in [
                ("object", "code"),
                ("linked", "code"),
                ("linked", "rodata"),
                ("linked", "unwind"),
            ]:
                x, y = a[container][key], b[container][key]
                values.append(
                    f"{x}/{y} ({(x / y - 1) * 100:+.1f}%)" if y else f"{x}/{y}"
                )
            lines.append(f"{compiler} | {optimization} | " + " | ".join(values))
    lines += [
        "",
        "Expected-only footprint (codec functions excluded): ours / tl (bytes)",
        "compiler | optimization | object code | linked code | linked rodata | linked unwind",
    ]
    for compiler in layouts:
        for optimization in OPTIMIZATIONS:
            a, b = (
                builds[f"{compiler}-{optimization}-{backend}"]["expected_only"]
                for backend in BACKENDS
            )
            lines.append(
                f"{compiler} | {optimization} | "
                + " | ".join(
                    f"{a[container][key]}/{b[container][key]}"
                    for container, key in [
                        ("object", "code"),
                        ("linked", "code"),
                        ("linked", "rodata"),
                        ("linked", "unwind"),
                    ]
                )
            )
    lines += ["", "Per exported kernel size at -O3: ours / tl (bytes)"]
    for compiler in layouts:
        a, b = (
            builds[f"{compiler}-O3-{backend}"]["object_symbols"] for backend in BACKENDS
        )
        for name in sorted(set(a) | set(b)):
            if name.startswith("cmp_"):
                lines.append(f"{compiler} | {name} | {a.get(name, 0)}/{b.get(name, 0)}")
    lines += [
        "",
        "Runtime at -O3: medians across all four layouts and paired rounds. Ratios are medians within each layout, ours / tl.",
        "compiler | case | ours ns/item | tl ns/item | aggregate change | layout ratio range",
    ]
    for compiler in layouts:
        cases = sorted({row["case"] for row in runtimes if row["compiler"] == compiler})
        for case in cases:
            subset = [
                row
                for row in runtimes
                if row["compiler"] == compiler and row["case"] == case
            ]
            a, b = (
                statistics.median(
                    row["ns"] for row in subset if row["backend"] == backend
                )
                for backend in BACKENDS
            )
            ratios = []
            for offset in meta["layouts"]:
                x, y = (
                    statistics.median(
                        row["ns"]
                        for row in subset
                        if row["backend"] == backend and row["offset"] == offset
                    )
                    for backend in BACKENDS
                )
                ratios.append(x / y)
            lines.append(
                f"{compiler} | {case} | {a:.3f} | {b:.3f} | {(a / b - 1) * 100:+.1f}% | {min(ratios):.3f}..{max(ratios):.3f}"
            )
    lines += [
        "",
        "Measurement boundaries",
        "Code sizes exclude debug sections. Kernel object code includes emitted helper functions; the linked executable also includes the common size driver and startup/runtime stubs. rodata and unwind/exception metadata are reported separately. Dynamic library code is not included. A symbol is a function body, so per-kernel sizes exclude out-of-line helpers. No LTO is used.",
        "Scalar-return and string-return rows include a noinline producer call, exposing return/consumer behavior. Materialized rows force object representation into memory with the same compiler barrier. String rows allocate a 32-character payload on success. checked_value uses exceptions for failed value() access. Container encode buffers are reserved; decode destinations clear while retaining capacity.",
        "Expected arithmetic fixtures are checked against independent plain-value calculations before timing. Codec fixtures round-trip typed values and compare exact source values; both smoke executables run via CTest. Runtime processes use CPU affinity, but frequency scaling and shared-host activity are not isolated. Per-layout ratios and raw samples should be inspected before making a regression claim. Relocation addresses change relative helper placement too; the sweep samples executable layouts, not a guarantee of identical inner-loop alignment between backends.",
    ]
    (output / "report.txt").write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tl-include", type=Path, default=os.environ.get("TL_EXPECTED_INCLUDE_DIR")
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "build" / "expected-comparison"
    )
    parser.add_argument("--compiler", action="append", default=None)
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument("--runs", type=int, default=5)
    args = parser.parse_args()
    tl_include = args.tl_include or Path("/usr/include")
    tl_include = tl_include.resolve()
    header = tl_include / "tl" / "expected.hpp"
    if not header.is_file():
        parser.error(
            "--tl-include must point to an installed or externally cached directory containing tl/expected.hpp"
        )
    if args.runs < 1 or args.cpu not in os.sched_getaffinity(0):
        parser.error(
            "runs must be positive and CPU must be in the process affinity set"
        )
    compilers = args.compiler or ["/usr/bin/g++", "/usr/bin/clang++"]
    if len({compiler_label(c) for c in compilers}) != len(compilers):
        parser.error("use at most one GCC and one Clang compiler per output directory")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    meta = {
        "source_commit": command(["git", "-C", ROOT, "rev-parse", "HEAD"]).strip(),
        "source_diff": command(["git", "-C", ROOT, "diff"]),
        "source_files": {
            str(p.relative_to(ROOT)): sha256(p)
            for p in sorted(SOURCE.iterdir())
            if p.is_file()
            and (p.suffix in {".cpp", ".h"} or p.name == "CMakeLists.txt")
        },
        "expected_header_sha256": sha256(ROOT / "include/cbor_tags/detail/expected.h"),
        "script_sha256": sha256(__file__),
        "python": {
            "version": sys.version,
            "path": sys.executable,
            "sha256": sha256(Path(sys.executable).resolve()),
        },
        "tl_include": str(tl_include),
        "tl_header_sha256": sha256(header),
        "cpu": args.cpu,
        "runs": args.runs,
        "layouts": [0, 16, 32, 48],
        "tools": {},
        "commands": [],
        "compile_commands": {},
        "host": command(["uname", "-a"]) + command(["lscpu"]),
    }
    for tool in [
        *compilers,
        "cmake",
        "ctest",
        "size",
        "nm",
        "objdump",
        "strip",
        "taskset",
    ]:
        exe = shutil.which(tool)
        if not exe:
            parser.error(f"required tool not found: {tool}")
        path = Path(exe).resolve()
        meta["tools"][tool] = {
            "path": str(path),
            "sha256": sha256(path),
            "version": command([exe, "--version"]),
        }
    governor = Path(f"/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_governor")
    meta["governor"] = (
        governor.read_text().strip() if governor.exists() else "unavailable"
    )
    builds, layouts = {}, {}
    for compiler in compilers:
        label = compiler_label(compiler)
        layouts[label] = {}
        for optimization in OPTIMIZATIONS:
            build = output / f"{label}-{optimization}"
            configure = [
                "cmake",
                "-S",
                SOURCE,
                "-B",
                build,
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_CXX_COMPILER={compiler}",
                f"-DTL_EXPECTED_INCLUDE_DIR={tl_include}",
                f"-DCMP_OPTIMIZATION=-{optimization}",
            ]
            meta["commands"].append([str(x) for x in configure])
            command(configure, output / f"{label}-{optimization}-configure.log")
            targets = [
                f"{kind}_{backend}"
                for kind in ["size", "size_expected", "layout"]
                for backend in BACKENDS
            ]
            if optimization == "O3":
                targets += [
                    f"runtime_{backend}_{offset}"
                    for backend in BACKENDS
                    for offset in meta["layouts"]
                ]
            cmd = ["cmake", "--build", build, "--parallel", "4", "--target", *targets]
            meta["commands"].append([str(x) for x in cmd])
            command(cmd, output / f"{label}-{optimization}-build.log")
            meta["compile_commands"][f"{label}-{optimization}"] = json.loads(
                (build / "compile_commands.json").read_text()
            )
            command(
                ["ctest", "--test-dir", build, "--output-on-failure"],
                output / f"{label}-{optimization}-ctest.log",
            )
            for backend in BACKENDS:
                obj = build / f"CMakeFiles/kernels_{backend}.dir/kernels.cpp.o"
                exe = build / f"size_{backend}"
                obj_sections, exe_sections = sections(obj), sections(exe)
                stripped = output / f"{label}-{optimization}-{backend}.stripped"
                command(["strip", "--strip-all", "-o", stripped, exe])
                builds[f"{label}-{optimization}-{backend}"] = {
                    "object": footprint(obj_sections),
                    "linked": footprint(exe_sections),
                    "object_sections": obj_sections,
                    "linked_sections": exe_sections,
                    "object_symbols": symbols(obj),
                    "linked_symbols": symbols(exe),
                    "file_bytes": exe.stat().st_size,
                    "stripped_file_bytes": stripped.stat().st_size,
                    "object_sha256": sha256(obj),
                    "executable_sha256": sha256(exe),
                }
                expected_obj = (
                    build / f"CMakeFiles/expected_only_{backend}.dir/kernels.cpp.o"
                )
                expected_exe = build / f"size_expected_{backend}"
                builds[f"{label}-{optimization}-{backend}"]["expected_only"] = {
                    "object": footprint(sections(expected_obj)),
                    "linked": footprint(sections(expected_exe)),
                    "object_symbols": symbols(expected_obj),
                    "object_sha256": sha256(expected_obj),
                    "executable_sha256": sha256(expected_exe),
                }
                command(
                    ["objdump", "-dr", "--no-show-raw-insn", obj],
                    output / f"{label}-{optimization}-{backend}.asm",
                )
                if optimization == "O3":
                    data = command([build / f"layout_{backend}"])
                    (output / f"{label}-{backend}-layout.csv").write_text(data)
                    layouts[label][backend] = {
                        row["type"]: {k: int(v) for k, v in row.items() if k != "type"}
                        for row in csv.DictReader(data.splitlines())
                    }
            print(f"Built and validated {label} -{optimization}", flush=True)
    meta["runtime_binaries_sha256"] = {
        str(p.relative_to(output)): sha256(p)
        for p in sorted(output.glob("*/runtime_*"))
        if p.is_file()
    }
    (output / "sizes.json").write_text(json.dumps(builds, indent=2))
    (output / "provenance.json").write_text(json.dumps(meta, indent=2))
    runtimes = []
    for run in range(args.runs):
        for compiler in compilers:
            label = compiler_label(compiler)
            build = output / f"{label}-O3"
            offsets = (
                meta["layouts"] if run % 2 == 0 else list(reversed(meta["layouts"]))
            )
            for offset in offsets:
                for backend in BACKENDS if run % 2 == 0 else tuple(reversed(BACKENDS)):
                    data = command(
                        [
                            "taskset",
                            "-c",
                            args.cpu,
                            build / f"runtime_{backend}_{offset}",
                        ]
                    )
                    (
                        output / f"{label}-{backend}-offset{offset}-run{run}.csv"
                    ).write_text(data)
                    for row in csv.DictReader(data.splitlines()):
                        runtimes.append(
                            {
                                "compiler": label,
                                "backend": backend,
                                "offset": offset,
                                "run": run,
                                "case": row["case"],
                                "ns": float(row["ns_per_item"]),
                            }
                        )
        print(f"Measured paired round {run + 1}/{args.runs}", flush=True)
    (output / "runtime.json").write_text(json.dumps(runtimes, indent=2))
    write_report(output, builds, runtimes, layouts, meta)
    print(output / "report.txt", flush=True)


if __name__ == "__main__":
    main()
