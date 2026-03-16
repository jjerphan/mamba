#!/usr/bin/env python3
"""
Optional helper to run perf record for selected (binary, spec, cache_state, cpu_mode).

This is intentionally simple: you can call it manually for combinations of interest.
"""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path
from typing import Literal


CpuMode = Literal["single", "multi"]
CacheState = Literal["cold", "warm"]


def build_record_command(
    micromamba_bin: str,
    spec: str,
    env_name: str,
    cpu_mode: CpuMode,
    output_data: Path,
) -> list[str]:
    base = [
        "perf",
        "record",
        "-g",
        "--call-graph",
        "dwarf",
        "-o",
        str(output_data),
        micromamba_bin,
        "create",
        "-n",
        env_name,
        spec,
        "--dry-run",
        "-y",
    ]
    if cpu_mode == "single":
        return ["taskset", "-c", "0"] + base
    return base


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Run perf record for a micromamba dry-run combination.",
    )
    p.add_argument(
        "--binary-label",
        choices=["old", "new"],
        required=True,
        help="Binary label (old or new).",
    )
    p.add_argument(
        "--micromamba-old",
        default=os.environ.get("MICROMAMBA_OLD"),
        help="Path to old binary.",
    )
    p.add_argument(
        "--micromamba-new",
        default=os.environ.get("MICROMAMBA_NEW"),
        help="Path to new binary.",
    )
    p.add_argument(
        "--spec",
        required=True,
        help="Spec to install in dry-run.",
    )
    p.add_argument(
        "--cache-state",
        choices=["cold", "warm"],
        default="warm",
        help="Cache state (informational).",
    )
    p.add_argument(
        "--cpu-mode",
        choices=["single", "multi"],
        default="single",
        help="CPU mode.",
    )
    p.add_argument(
        "--output-dir",
        type=Path,
        default=Path("micromamba-perf-runs/profiles"),
        help="Directory for perf.data files.",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.binary_label == "old":
        micromamba = args.micromamba_old
    else:
        micromamba = args.micromamba_new
    if not micromamba:
        raise SystemExit(
            "Micromamba path not specified (use --micromamba-old/--micromamba-new or env).",
        )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    env_name = f"perf_{args.spec}_{args.cache_state}_{args.cpu_mode}_record"
    output_data = args.output_dir / (
        f"perf-{args.binary_label}-{args.spec}-{args.cache_state}-{args.cpu_mode}.data"
    )

    cmd = build_record_command(micromamba, args.spec, env_name, args.cpu_mode, output_data)
    rc = subprocess.call(cmd)
    return int(rc)


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
