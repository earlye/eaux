#!/usr/bin/env python3
"""Benchmark the committed tree with every C++ compiler on this host and append the results to
bench.jsonl, one row per (commit, compiler, case).

    python3 c++/text/static_format/bench/record.py [--cpu N] [--cmake CMAKE]

`make bench-record` in c++/ runs this. It refuses to run while tracked files have changes,
because each row names HEAD as what was measured; untracked files are ignored.

Compilers: CMake's default for the host (MSVC in a Visual Studio developer environment), plus
every g++/clang++ (and versioned g++-N/clang++-N) on PATH, plus `c++` on macOS. Each one that
configures and builds is benchmarked; the rest are skipped with a note. Two candidates that
CMake identifies as the same compiler id and version are measured once.

Each row: commitHash, cpu, ram (total physical memory in bytes), uname (system, release,
machine, like `uname -srm`), compiler (CMake's compiler id and version), case, ts (UTC, when the
run finished), pinned (whether the run was pinned to one CPU), then the median real time in ns
of snprintf, std_format_to, fmt_FMT_COMPILE, eaux_pfmt and eaux_bfmt, null where a contender has
no benchmark for the case. In "/string" cases the std_format_to column is std::format.

Only the Linux paths have been run; the macOS and Windows paths are written to the platforms'
documented interfaces but untested.
"""

import argparse
import ctypes
import datetime
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

CXX_ROOT = pathlib.Path(__file__).resolve().parents[3]
LOG = pathlib.Path(__file__).resolve().parent / "bench.jsonl"
BENCH_ARGS = ["--benchmark_repetitions=5", "--benchmark_report_aggregates_only=true", "--benchmark_min_time=0.3s"]

# bench.cpp names each benchmark <prefix>_<contender>; the prefix identifies the case.
CASES = {
    "hex": "hex/buffer",
    "float": "float/buffer",
    "mixed": "mixed/buffer",
    "row": "row30/buffer",
    "row_ascii": "row30-ascii/buffer",
    "string_hex": "hex/string",
    "string_mixed": "mixed/string",
    "string_row": "row30/string",
}
CONTENDERS = {
    "snprintf": "snprintf",
    "std_format_to": "std_format_to",
    "std_format": "std_format_to",
    "fmt_compile": "fmt_FMT_COMPILE",
    "pfmt": "eaux_pfmt",
    "bfmt": "eaux_bfmt",
}
COLUMNS = ["snprintf", "std_format_to", "fmt_FMT_COMPILE", "eaux_pfmt", "eaux_bfmt"]


def note(message):
    print(f"record.py: {message}", file=sys.stderr)


def git(*args):
    return subprocess.run(["git", "-C", str(CXX_ROOT), *args], check=True, capture_output=True, text=True).stdout


def cpu_name():
    system = platform.system()
    if system == "Linux":
        for line in pathlib.Path("/proc/cpuinfo").read_text().splitlines():
            key, _, value = line.partition(":")
            if key.strip() == "model name":
                return value.strip()
    elif system == "Darwin":  # untested
        return subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True).stdout.strip()
    elif system == "Windows":  # untested
        import winreg

        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
        return winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    return platform.processor()


def ram_bytes():
    system = platform.system()
    if system == "Linux":
        for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                value, unit = line.split()[1:3]
                return int(value) * {"kB": 1024}.get(unit, 1)
    elif system == "Darwin":  # untested
        return int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout)
    elif system == "Windows":  # untested

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong),
                        ("available", ctypes.c_ulonglong), ("total_page", ctypes.c_ulonglong),
                        ("available_page", ctypes.c_ulonglong), ("total_virtual", ctypes.c_ulonglong),
                        ("available_virtual", ctypes.c_ulonglong), ("available_extended", ctypes.c_ulonglong)]

        status = MemoryStatus(length=ctypes.sizeof(MemoryStatus))
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
        return status.total
    return None


def candidates():
    """Compiler executables to try; None means CMake's default for this host."""
    found = [None]
    names = ["g++", "clang++"] + (["c++"] if platform.system() == "Darwin" else [])
    versioned = re.compile(r"^(g\+\+|clang\+\+)-\d+(\.exe)?$")
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        try:
            names += sorted(entry.name for entry in os.scandir(directory) if versioned.match(entry.name))
        except OSError:
            continue
    seen = set()
    for name in names:
        path = shutil.which(name)
        if path and os.path.realpath(path) not in seen:
            seen.add(os.path.realpath(path))
            found.append(path)
    return found


def cmake_compiler(build):
    """CMake's own identification of the compiler it configured, e.g. "GNU 15.2.0"."""
    for info in build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"):
        text = info.read_text()
        id_ = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]*)"\)', text)
        version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]*)"\)', text)
        if id_ and version:
            return f"{id_.group(1)} {version.group(1)}"
    return None


def build_bench(cmake, cxx, build):
    """Configures and builds the benchmark; returns (compiler, executable) or None."""
    label = cxx or "CMake's default compiler"
    configure = [cmake, "-S", str(CXX_ROOT), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                 "-DEAUX_BUILD_TESTS=OFF", "-DEAUX_BUILD_BENCH=ON"]
    if cxx:
        configure.append(f"-DCMAKE_CXX_COMPILER={cxx}")
    if platform.system() != "Windows" and shutil.which("ninja"):
        configure += ["-G", "Ninja"]
    if subprocess.run(configure, capture_output=True).returncode != 0:
        note(f"skipping {label}: CMake could not configure with it")
        return None
    compiler = cmake_compiler(build)
    if subprocess.run([cmake, "--build", str(build), "--config", "Release", "--parallel"], capture_output=True).returncode != 0:
        note(f"skipping {label} ({compiler}): the benchmark did not build")
        return None
    executable = next((p for p in build.glob("text/static_format/bench/**/eaux_text_static_format_bench*")
                       if p.is_file() and p.suffix in ("", ".exe")), None)
    if executable is None:
        note(f"skipping {label} ({compiler}): no benchmark executable found in {build}")
        return None
    return compiler, executable


def run_bench(executable, results, cpu):
    """Runs the benchmark, pinned to `cpu` where the platform allows; returns whether it was."""
    command = [str(executable), *BENCH_ARGS, f"--benchmark_out={results}", "--benchmark_out_format=json"]
    system = platform.system()
    if system == "Linux" and hasattr(os, "sched_setaffinity"):
        subprocess.run(command, check=True, preexec_fn=lambda: os.sched_setaffinity(0, {cpu}))
        return True
    if system == "Windows":  # untested: pinned right after start, before the measurements begin
        process = subprocess.Popen(command)
        pinned = bool(ctypes.windll.kernel32.SetProcessAffinityMask(int(process._handle), 1 << cpu))
        if process.wait() != 0:
            raise SystemExit(f"{executable} failed")
        return pinned
    subprocess.run(command, check=True)  # macOS has no CPU affinity API
    return False


def rows(results, base):
    by_case = {}
    for bench in json.loads(results.read_text())["benchmarks"]:
        if bench.get("aggregate_name") != "median":
            continue
        if bench["time_unit"] != "ns":
            raise SystemExit(f"{bench['name']}: expected ns, got {bench['time_unit']}")
        name = bench["run_name"]
        # Longest case prefix first, so "row_ascii_pfmt" is not read as case "row".
        prefix = next((p for p in sorted(CASES, key=len, reverse=True)
                       if name.startswith(p + "_") and name[len(p) + 1:] in CONTENDERS), None)
        if prefix is None:
            raise SystemExit(f"benchmark {name!r} matches no known case and contender; update record.py")
        column = CONTENDERS[name[len(prefix) + 1:]]
        by_case.setdefault(CASES[prefix], dict.fromkeys(COLUMNS))[column] = round(bench["real_time"], 3)
    return [{**base, "case": case, **columns} for case, columns in by_case.items()]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cpu", type=int, default=5, help="CPU to pin to where possible (default: %(default)s)")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--build", type=pathlib.Path, default=CXX_ROOT / "build" / "record")
    args = parser.parse_args()

    if git("status", "--porcelain", "--untracked-files=no").strip():
        raise SystemExit("record.py: commit or discard tracked changes first; each row names HEAD")
    commit = git("rev-parse", "HEAD").strip()
    host = {
        "commitHash": commit,
        "cpu": cpu_name(),
        "ram": ram_bytes(),
        "uname": f"{platform.system()} {platform.release()} {platform.machine()}",
    }

    measured = set()
    lines = []
    for index, cxx in enumerate(candidates()):
        build = args.build / f"compiler-{index}"
        built = build_bench(args.cmake, cxx, build)
        if built is None:
            continue
        compiler, executable = built
        if compiler in measured:
            note(f"skipping {cxx or 'the default'}: {compiler} is already measured")
            continue
        measured.add(compiler)
        print(f"record.py: benchmarking {compiler} ({cxx or 'default'})", flush=True)
        results = build / "results.json"
        pinned = run_bench(executable, results, args.cpu)
        ts = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")
        lines += [json.dumps(row) for row in rows(results, {**host, "compiler": compiler, "case": None, "ts": ts,
                                                             "pinned": pinned})]
    if not lines:
        raise SystemExit("record.py: no compiler could build the benchmark")
    with LOG.open("a", encoding="utf-8") as out:
        out.write("".join(line + "\n" for line in lines))
    print(f"record.py: appended {len(lines)} rows for {commit[:12]} to {LOG}")


if __name__ == "__main__":
    main()
