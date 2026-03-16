#!/usr/bin/env python3
"""
Micromamba perf(1) Benchmark Harness.

Runs dry-run installs for a matrix of:
  - binaries: MICROMAMBA_OLD, MICROMAMBA_NEW
  - specs: xtensor, python, jupyterlab, pyarrow, skore
  - cpu_mode: single (taskset -c 0), multi (no affinity)
  - cache_state: cold (caches cleared), warm (caches reused)

For each combination it performs:
  - configurable warmup runs (no perf/time logging)
  - N measured runs with `perf stat` + `/usr/bin/time -v`

Outputs go under an output directory:
  - logs/: micromamba stdout/stderr
  - perf-stat/: perf stat CSV-like output
  - time/: /usr/bin/time -v stderr
  - manifest.json: configuration and version information

Optional perf record runs and HTML reporting are handled by other scripts.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path
from typing import Literal


CpuMode = Literal["single", "multi"]
CacheState = Literal["cold", "warm"]


DEFAULT_SPECS = ["xtensor", "python", "jupyterlab", "pyarrow", "skore"]

# Event set based on the plan. We avoid syscall tracepoints by default since they
# often require elevated permissions; page-fault and time/IO counters already give
# good visibility into I/O behavior.
PERF_EVENTS = [
    "task-clock",
    "cycles",
    "instructions",
    "branches",
    "branch-misses",
    "minor-faults",
    "major-faults",
    "page-faults",
    "context-switches",
    "cpu-migrations",
]


@dataclass
class BinaryConfig:
    label: str
    path: str
    version: str


def detect_version(path: str) -> str:
    try:
        out = subprocess.check_output([path, "--version"], text=True, stderr=subprocess.STDOUT)
    except Exception as exc:  # pragma: no cover - defensive
        return f"unknown (error: {exc})"
    return out.strip()


def run_cmd(
    argv: list[str],
    cwd: Path,
    stdout_path: Path,
    stderr_path: Path,
) -> int:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    with stdout_path.open("w") as out_f, stderr_path.open("w") as err_f:
        proc = subprocess.run(argv, cwd=str(cwd), stdout=out_f, stderr=err_f)
    return proc.returncode


def clear_caches(root_prefix: Path, pkgs_dirs: list[Path]) -> None:
    if root_prefix.exists():
        shutil.rmtree(root_prefix)
    for d in pkgs_dirs:
        if d.exists():
            shutil.rmtree(d)


def build_perf_command(
    micromamba_bin: str,
    spec: str,
    env_name: str,
    cpu_mode: CpuMode,
) -> list[str]:
    micromamba_cmd = [
        micromamba_bin,
        "create",
        "-n",
        env_name,
        spec,
        "--dry-run",
        "-y",
    ]

    perf_cmd = [
        "perf",
        "stat",
        "-x,",
        "-e",
        ",".join(PERF_EVENTS),
    ] + micromamba_cmd

    if cpu_mode == "single":
        return ["taskset", "-c", "0"] + perf_cmd
    else:
        return perf_cmd


def wrap_with_time(perf_argv: list[str]) -> list[str]:
    # /usr/bin/time -v wraps the perf invocation.
    return ["/usr/bin/time", "-v"] + perf_argv


def ensure_env(root_prefix: Path, extra_env: dict[str, str]) -> dict[str, str]:
    env = os.environ.copy()
    env["MAMBA_ROOT_PREFIX"] = str(root_prefix)
    # Ensure both binaries see the same configuration; default to ~/.condarc if present.
    from pathlib import Path as _Path  # local import to avoid top-level dependency

    if "CONDARC" not in env:
        condarc = _Path.home() / ".condarc"
        if condarc.exists():
            env["CONDARC"] = str(condarc)
    # Allow overriding CONDA_PKGS_DIRS via env if desired.
    env.update(extra_env)
    return env


def run_single_combination(
    binary: BinaryConfig,
    spec: str,
    cpu_mode: CpuMode,
    cache_state: CacheState,
    warmups: int,
    runs: int,
    work_dir: Path,
    out_dir: Path,
    root_prefix: Path,
    pkgs_dirs: list[Path],
) -> None:
    label = binary.label
    base_name = f"{label}-{spec}-{cache_state}-{cpu_mode}"

    print(
        f"[INFO] Starting {label} spec={spec} cache={cache_state} cpu_mode={cpu_mode} "
        f"(warmups={warmups}, runs={runs})",
        flush=True,
    )

    # Handle cache state.
    if cache_state == "cold":
        clear_caches(root_prefix, pkgs_dirs)

    # Warmups (no logging).
    for i in range(1, warmups + 1):
        env_name = f"perf_{spec}_warmup_{i}"
        perf_cmd = build_perf_command(binary.path, spec, env_name, cpu_mode)
        full_cmd = wrap_with_time(perf_cmd)
        env = ensure_env(root_prefix, {})
        subprocess.run(
            full_cmd,
            cwd=str(work_dir),
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    # Measured runs.
    for i in range(1, runs + 1):
        run_tag = f"{base_name}-run{i}"
        env_name = f"perf_{spec}_{cache_state}_{cpu_mode}_{i}"

        perf_cmd = build_perf_command(binary.path, spec, env_name, cpu_mode)
        full_cmd = wrap_with_time(perf_cmd)

        env = ensure_env(root_prefix, {})

        log_stdout = out_dir / "logs" / f"{run_tag}.log"
        log_stderr = out_dir / "time" / f"{run_tag}.time"
        # perf stat writes to stderr, so we redirect stderr to the time file.
        perf_stat_path = out_dir / "perf-stat" / f"perf-stat-{run_tag}.txt"

        # We want perf's stderr as well; use a simple tee-like approach:
        # run /usr/bin/time -v perf stat ... 2> time_file, and also ask perf
        # to dump metrics into perf-stat file via -o if available. To keep it
        # simple and portable with our chosen -x, capture stderr once and then
        # copy for parsing later.

        # Here we just run and split after: stderr contains both time and perf.
        combined_err = out_dir / "raw" / f"{run_tag}.stderr"
        combined_err.parent.mkdir(parents=True, exist_ok=True)

        log_stdout.parent.mkdir(parents=True, exist_ok=True)

        with log_stdout.open("w") as out_f, combined_err.open("w") as err_f:
            proc = subprocess.run(full_cmd, cwd=str(work_dir), env=env, stdout=out_f, stderr=err_f)

        # Split combined stderr into time vs perf later during parsing.
        # For now, store a copy as "perf-stat" placeholder so that downstream
        # tools have a consistent path to read from.
        perf_stat_path.parent.mkdir(parents=True, exist_ok=True)
        if not perf_stat_path.exists():
            shutil.copy(combined_err, perf_stat_path)

        # Store the time-style stderr as well (for now, same as combined).
        log_stderr.parent.mkdir(parents=True, exist_ok=True)
        if not log_stderr.exists():
            shutil.copy(combined_err, log_stderr)

        if proc.returncode != 0:
            print(
                f"[WARN] Command failed for {run_tag} with exit code {proc.returncode}",
                file=sys.stderr,
                flush=True,
            )
        else:
            print(f"[INFO] Completed {run_tag}", flush=True)


def write_manifest(
    out_dir: Path,
    binaries: list[BinaryConfig],
    specs: list[str],
    warmups: int,
    runs: int,
    root_prefix: Path,
    pkgs_dirs: list[Path],
) -> None:
    manifest = {
        "created_at": datetime.utcnow().isoformat() + "Z",
        "binaries": [asdict(b) for b in binaries],
        "specs": specs,
        "warmups": warmups,
        "runs": runs,
        "root_prefix": str(root_prefix),
        "pkgs_dirs": [str(p) for p in pkgs_dirs],
        "perf_events": PERF_EVENTS,
        "environment": {
            "hostname": os.uname().nodename,
            "kernel": os.uname().release,
        },
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Micromamba perf(1) benchmark harness.")
    parser.add_argument(
        "--micromamba-old",
        dest="micromamba_old",
        default=os.environ.get("MICROMAMBA_OLD"),
        help="Path to reference micromamba binary (or set MICROMAMBA_OLD).",
    )
    parser.add_argument(
        "--micromamba-new",
        dest="micromamba_new",
        default=os.environ.get("MICROMAMBA_NEW"),
        help="Path to candidate micromamba binary (or set MICROMAMBA_NEW).",
    )
    parser.add_argument(
        "--spec",
        dest="specs",
        action="append",
        help="Spec to benchmark (can be given multiple times). Defaults to the plan list.",
    )
    parser.add_argument(
        "--warmups",
        type=int,
        default=1,
        help="Number of warmup runs per (binary, spec, cpu_mode, cache_state).",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="Number of measured runs per (binary, spec, cpu_mode, cache_state).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("micromamba-perf-runs"),
        help="Output directory for logs and metrics.",
    )
    parser.add_argument(
        "--root-prefix",
        type=Path,
        default=Path("micromamba-perf-root"),
        help="Root prefix directory used by micromamba (will be created/removed).",
    )
    parser.add_argument(
        "--pkgs-dir",
        dest="pkgs_dirs",
        action="append",
        type=Path,
        help="Additional package cache directory to clear between cold runs (can be repeated).",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if not args.micromamba_old or not args.micromamba_new:
        print(
            "ERROR: both --micromamba-old and --micromamba-new (or MICROMAMBA_OLD/NEW) must be set.",
            file=sys.stderr,
        )
        return 1

    specs = args.specs if args.specs else list(DEFAULT_SPECS)
    out_dir: Path = args.out_dir
    root_prefix: Path = args.root_prefix
    pkgs_dirs: list[Path] = args.pkgs_dirs or []

    binaries = [
        BinaryConfig(
            label="old",
            path=args.micromamba_old,
            version=detect_version(args.micromamba_old),
        ),
        BinaryConfig(
            label="new",
            path=args.micromamba_new,
            version=detect_version(args.micromamba_new),
        ),
    ]

    print("[INFO] Writing manifest and starting benchmark runs...", flush=True)
    write_manifest(out_dir, binaries, specs, args.warmups, args.runs, root_prefix, pkgs_dirs)

    work_dir = Path.cwd()

    cpu_modes: tuple[CpuMode, ...] = ("single", "multi")
    cache_states: tuple[CacheState, ...] = ("cold", "warm")

    for spec in specs:
        for cache_state in cache_states:
            for cpu_mode in cpu_modes:
                # Interleave binaries within each (spec, cache_state, cpu_mode).
                for binary in binaries:
                    run_single_combination(
                        binary=binary,
                        spec=spec,
                        cpu_mode=cpu_mode,
                        cache_state=cache_state,
                        warmups=args.warmups,
                        runs=args.runs,
                        work_dir=work_dir,
                        out_dir=out_dir,
                        root_prefix=root_prefix,
                        pkgs_dirs=pkgs_dirs,
                    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
