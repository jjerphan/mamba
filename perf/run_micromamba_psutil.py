#!/usr/bin/env python3
"""
Micromamba psutil-based benchmark harness.

Runs dry-run installs (multithreaded only) for a matrix of:
  - binaries: MICROMAMBA_OLD, MICROMAMBA_NEW
  - specs: python, xtensor, jupyterlab (default)
  - cache_state: cold (caches cleared) and warm (caches reused)

For each combination it performs:
  - configurable warmup runs (no metric recording)
  - N measured runs with wall time, peak RSS, and network I/O via psutil

Results are written to:
  micromamba-psutil-runs/results.csv
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

import psutil  # type: ignore[import-untyped]


CacheState = Literal["cold", "warm"]


# Default specs limited to those requested in the psutil plan.
DEFAULT_SPECS = ["python", "xtensor", "jupyterlab", "scikit-learn"]


@dataclass
class BinaryConfig:
    label: str
    path: str
    version: str


def detect_version(path: str) -> str:
    try:
        out = subprocess.check_output([path, "--version"], text=True, stderr=subprocess.STDOUT)
    except Exception as exc:  # pragma: no cover
        return f"unknown (error: {exc})"
    return out.strip()


def ensure_env(root_prefix: Path, extra_env: dict[str, str]) -> dict[str, str]:
    env = os.environ.copy()
    env["MAMBA_ROOT_PREFIX"] = str(root_prefix)
    # Ensure both binaries share the same configuration; default to ~/.condarc if present.
    condarc = Path.home() / ".condarc"
    if "CONDARC" not in env and condarc.exists():
        env["CONDARC"] = str(condarc)
    env.update(extra_env)
    return env


def clear_caches_cold(root_prefix: Path, pkgs_dirs: list[Path]) -> None:
    if root_prefix.exists():
        shutil.rmtree(root_prefix)
    for d in pkgs_dirs:
        if d.exists():
            shutil.rmtree(d)
    # Also clear the user cache for conda as requested.
    conda_cache = Path.home() / ".cache" / "conda"
    if conda_cache.exists():
        shutil.rmtree(conda_cache)


def build_command(micromamba_bin: str, spec: str, env_name: str) -> list[str]:
    return [
        micromamba_bin,
        "create",
        "-n",
        env_name,
        spec,
        "--dry-run",
        "-y",
    ]


def measure_run(
    cmd: list[str],
    work_dir: Path,
    env: dict[str, str],
    stdout_path: Path,
    stderr_path: Path,
) -> dict[str, float | int]:
    """
    Spawn micromamba, measure wall time, peak RSS, and network I/O with psutil.
    Network I/O is system-wide delta (bytes_sent, bytes_recv) during the run.
    """
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)

    net_before = psutil.net_io_counters()
    bytes_sent_before = net_before.bytes_sent
    bytes_recv_before = net_before.bytes_recv

    start = time.monotonic()
    with stdout_path.open("w") as out_f, stderr_path.open("w") as err_f:
        proc = subprocess.Popen(cmd, cwd=str(work_dir), env=env, stdout=out_f, stderr=err_f)

    peak_rss = 0
    cpu_user = 0.0
    cpu_system = 0.0

    ps_proc = psutil.Process(proc.pid)
    try:
        while True:
            if proc.poll() is not None:
                break
            try:
                with ps_proc.oneshot():
                    mem = ps_proc.memory_info()
                    rss = mem.rss
                    if rss > peak_rss:
                        peak_rss = rss
                    times = ps_proc.cpu_times()
                    cpu_user = max(cpu_user, float(times.user))
                    cpu_system = max(cpu_system, float(times.system))
            except psutil.NoSuchProcess:
                break
            time.sleep(0.05)
    finally:
        proc.wait()

    end = time.monotonic()
    wall_time_s = end - start

    net_after = psutil.net_io_counters()
    net_bytes_sent = net_after.bytes_sent - bytes_sent_before
    net_bytes_recv = net_after.bytes_recv - bytes_recv_before

    return {
        "wall_time_s": wall_time_s,
        "peak_rss_bytes": float(peak_rss),
        "cpu_user_s": cpu_user,
        "cpu_system_s": cpu_system,
        "net_bytes_sent": int(net_bytes_sent),
        "net_bytes_recv": int(net_bytes_recv),
        "exit_code": int(proc.returncode),
    }


def run_single_combination(
    binary: BinaryConfig,
    spec: str,
    cache_state: CacheState,
    warmups: int,
    runs: int,
    work_dir: Path,
    out_dir: Path,
    root_prefix: Path,
    pkgs_dirs: list[Path],
    results: list[dict[str, object]],
) -> None:
    label = binary.label
    base_name = f"{label}-{spec}-{cache_state}"

    print(
        f"[INFO] Starting {label} spec={spec} cache={cache_state} (warmups={warmups}, runs={runs})",
        flush=True,
    )

    # Warmups (no recording).
    for i in range(1, warmups + 1):
        env_name = f"psutil_{spec}_warmup_{i}"
        cmd = build_command(binary.path, spec, env_name)
        env = ensure_env(root_prefix, {})
        try:
            subprocess.run(
                cmd,
                cwd=str(work_dir),
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except Exception as exc:  # pragma: no cover
            print(
                f"[WARN] Warmup failed for {label} {spec} ({exc})",
                file=sys.stderr,
                flush=True,
            )

    # Measured runs.
    logs_dir = out_dir / "logs"
    for i in range(1, runs + 1):
        run_tag = f"{base_name}-run{i}"
        env_name = f"psutil_{spec}_{cache_state}_{i}"

        # For cold cache, clear caches immediately before each measured run.
        if cache_state == "cold":
            print(
                f"[INFO] Clearing caches before run {run_tag} (cold cache).",
                flush=True,
            )
            clear_caches_cold(root_prefix, pkgs_dirs)

        cmd = build_command(binary.path, spec, env_name)
        env = ensure_env(root_prefix, {})

        stdout_path = logs_dir / f"{run_tag}.stdout"
        stderr_path = logs_dir / f"{run_tag}.stderr"

        metrics = measure_run(cmd, work_dir, env, stdout_path, stderr_path)

        row: dict[str, object] = {
            "binary": label,
            "spec": spec,
            "cache_state": cache_state,
            "run_id": i,
            **metrics,
        }
        results.append(row)

        if metrics["exit_code"] != 0:
            print(
                f"[WARN] Command failed for {run_tag} with exit code {metrics['exit_code']}",
                file=sys.stderr,
                flush=True,
            )
        else:
            print(
                f"[INFO] Completed {run_tag} (wall_time_s={metrics['wall_time_s']:.3f})",
                flush=True,
            )


def write_results_csv(out_dir: Path, rows: list[dict[str, object]]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    csv_path = out_dir / "results.csv"
    if not rows:
        csv_path.write_text("")
        return
    fieldnames = list(rows[0].keys())
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Micromamba psutil-based benchmark harness.")
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
        help="Number of warmup runs per (binary, spec, cache_state).",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=5,
        help="Number of measured runs per (binary, spec, cache_state).",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("micromamba-psutil-runs"),
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

    print("[INFO] Starting psutil-based micromamba benchmark runs (multithreaded)...", flush=True)

    work_dir = Path.cwd()
    results: list[dict[str, object]] = []

    cache_states: tuple[CacheState, ...] = ("cold", "warm")

    for spec in specs:
        for cache_state in cache_states:
            for binary in binaries:
                run_single_combination(
                    binary=binary,
                    spec=spec,
                    cache_state=cache_state,
                    warmups=args.warmups,
                    runs=args.runs,
                    work_dir=work_dir,
                    out_dir=out_dir,
                    root_prefix=root_prefix,
                    pkgs_dirs=pkgs_dirs,
                    results=results,
                )

    write_results_csv(out_dir, results)
    print(f"[INFO] Wrote results to {out_dir / 'results.csv'}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
