#!/usr/bin/env python3
"""
Plot memory usage over time from perf/run_micromamba_psutil.py timeseries CSVs.

Columns: t_ms,rss_bytes,rss_total_bytes,vms_bytes,...

Example:
  python3 perf/plot_psutil_timeseries.py \\
    micromamba-psutil-runs-libsolvstats3/timeseries/new-pyarrow-warm-run1.csv \\
    -o micromamba-psutil-runs-libsolvstats3/timeseries/new-pyarrow-warm-run1.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def mib(x: float) -> float:
    return x / (1024.0 * 1024.0)


def read_timeseries(csv_path: Path) -> tuple[list[float], list[float], list[float] | None]:
    t_s: list[float] = []
    rss_mib: list[float] = []
    rss_tot_mib: list[float] | None = None
    with csv_path.open(newline="", encoding="utf-8") as f:
        r = csv.DictReader(f)
        if "rss_total_bytes" in (r.fieldnames or []):
            rss_tot_mib = []
        for row in r:
            t_s.append(float(row["t_ms"]) / 1000.0)
            rss_mib.append(mib(float(row["rss_bytes"])))
            if rss_tot_mib is not None:
                rss_tot_mib.append(mib(float(row["rss_total_bytes"])))
    return t_s, rss_mib, rss_tot_mib


def plot_csv(csv_path: Path, out_path: Path | None) -> None:
    t_s, rss_mib, rss_tot_mib = read_timeseries(csv_path)

    fig, ax = plt.subplots(figsize=(10, 5), dpi=120)
    ax.plot(t_s, rss_mib, label="RSS (process)", color="#0072B2", linewidth=1.2)
    if rss_tot_mib is not None:
        ax.plot(
            t_s,
            rss_tot_mib,
            label="RSS (process + children)",
            color="#E69F00",
            linewidth=1.0,
            alpha=0.9,
        )
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Memory (MiB)")
    ax.set_title(f"Memory over time — {csv_path.name}")
    ax.legend(loc="upper left")
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)
    fig.tight_layout()

    out = out_path or csv_path.with_suffix(".png")
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"Wrote {out}")


def main() -> int:
    p = argparse.ArgumentParser(description="Plot psutil timeseries CSVs (RSS over time).")
    p.add_argument("csv", nargs="+", type=Path, help="One or more timeseries .csv files")
    p.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="Output PNG path (only valid with a single input CSV).",
    )
    args = p.parse_args()

    if len(args.csv) > 1 and args.output is not None:
        raise SystemExit("Use --output only with a single input CSV.")

    for csv_path in args.csv:
        if not csv_path.exists():
            print(f"Skip missing: {csv_path}", flush=True)
            continue
        out = args.output if len(args.csv) == 1 else None
        plot_csv(csv_path, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
