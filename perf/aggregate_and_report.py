#!/usr/bin/env python3
"""
Aggregate perf(1) and /usr/bin/time outputs and generate an HTML report.

This script expects the directory layout produced by run_micromamba_perf.py:

  micromamba-perf-runs/
    manifest.json
    logs/
    perf-stat/
    time/
    raw/

It produces:
  - aggregated.csv: tidy table of metrics
  - report.html: self-contained HTML report with per-spec comparisons
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, asdict
from pathlib import Path

import pandas as pd  # type: ignore[import-untyped]
import plotly.express as px  # type: ignore[import-untyped]
from plotly.offline import plot as plot_html  # type: ignore[import-untyped]


RUN_TAG_RE = re.compile(
    r"^(?P<binary>old|new)-(?P<spec>[^-]+)-(?P<cache_state>cold|warm)-"
    r"(?P<cpu_mode>single|multi)-run(?P<run_id>\d+)$"
)


@dataclass
class RunKey:
    binary: str
    spec: str
    cache_state: str
    cpu_mode: str
    run_id: int


def parse_run_tag(tag: str) -> RunKey | None:
    m = RUN_TAG_RE.match(tag)
    if not m:
        return None
    return RunKey(
        binary=m.group("binary"),
        spec=m.group("spec"),
        cache_state=m.group("cache_state"),
        cpu_mode=m.group("cpu_mode"),
        run_id=int(m.group("run_id")),
    )


def parse_time_file(path: Path) -> dict[str, float]:
    """
    Parse /usr/bin/time -v output (mixed with perf stat stderr).

    We filter for lines starting with ' ' and containing ':' and
    store them as key -> numeric value when possible.
    """
    metrics: dict[str, float] = {}
    if not path.exists():
        return metrics
    with path.open() as f:
        for line in f:
            line = line.strip()
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            key = key.strip()
            value = value.strip()
            # Keep a few interesting keys; ignore parse failures.
            if key in {
                "Elapsed (wall clock) time (h:mm:ss or m:ss)",
                "Maximum resident set size (kbytes)",
                "File system inputs",
                "File system outputs",
                "Major (requiring I/O) page faults",
                "Minor (reclaiming a frame) page faults",
            }:
                # Wall time has h:mm:ss or m:ss, convert to seconds.
                if "Elapsed (wall clock)" in key:
                    parts = value.split(":")
                    try:
                        if len(parts) == 3:
                            h, m, s = parts
                            secs = int(h) * 3600 + int(m) * 60 + float(s)
                        elif len(parts) == 2:
                            m, s = parts
                            secs = int(m) * 60 + float(s)
                        else:
                            secs = float(value)
                        metrics["wall_time_s"] = secs
                    except ValueError:
                        continue
                else:
                    try:
                        metrics[key] = float(value)
                    except ValueError:
                        continue
    return metrics


def parse_perf_file(path: Path) -> dict[str, float]:
    """
    Parse perf stat -x, output style:
      value,unit,event,comment
    We keep a small subset of key counters.
    """
    metrics: dict[str, float] = {}
    if not path.exists():
        return metrics
    with path.open() as f:
        for line in f:
            parts = [p.strip() for p in line.strip().split(",")]
            if len(parts) < 3:
                continue
            value_str, _unit, event = parts[:3]
            if value_str in ("<not supported>", "<not counted>", ""):
                continue
            try:
                value = float(value_str)
            except ValueError:
                continue
            if event in {
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
            }:
                metrics[event] = value
    if "instructions" in metrics and "cycles" in metrics and metrics["cycles"] > 0:
        metrics["ipc"] = metrics["instructions"] / metrics["cycles"]
    return metrics


def aggregate_runs(out_dir: Path) -> pd.DataFrame:
    """
    Aggregate results by walking the time/*.time files.

    Each .time file contains both perf stat CSV output (at the top) and
    `/usr/bin/time -v` human-readable metrics (afterwards). We treat the .time
    file as the single source of truth so that wall-clock timing always matches
    what the user sees in these files.
    """
    rows: list[dict[str, object]] = []
    time_dir = out_dir / "time"

    for time_file in sorted(time_dir.glob("*.time")):
        tag = time_file.stem
        key = parse_run_tag(tag)
        if key is None:
            continue

        time_metrics = parse_time_file(time_file)
        perf_metrics = parse_perf_file(time_file)

        row: dict[str, object] = asdict(key)
        row.update(time_metrics)
        row.update(perf_metrics)
        rows.append(row)

    if not rows:
        raise SystemExit(f"No runs found in {time_dir}")

    df = pd.DataFrame(rows)

    # Fallback: if wall_time_s could not be parsed from /usr/bin/time -v output,
    # approximate it from perf's task-clock (reported in msec).
    if "wall_time_s" not in df.columns and "task-clock" in df.columns:
        df["wall_time_s"] = df["task-clock"] / 1000.0

    return df


def make_plots(df: pd.DataFrame) -> dict[str, str]:
    """
    Build Plotly figures and return a dict of HTML divs keyed by logical name.
    """
    html: dict[str, str] = {}

    # Overall comparison: wall time by binary/spec/cache_state/cpu_mode.
    if "wall_time_s" in df.columns:
        fig = px.box(
            df,
            x="spec",
            y="wall_time_s",
            color="binary",
            facet_row="cache_state",
            facet_col="cpu_mode",
            title="Wall time (s) by spec / binary / cache state / CPU mode",
        )
        html["wall_time"] = plot_html(fig, include_plotlyjs=False, output_type="div")

    # Peak RSS if available.
    rss_col = "Maximum resident set size (kbytes)"
    if rss_col in df.columns:
        fig = px.box(
            df,
            x="spec",
            y=rss_col,
            color="binary",
            facet_row="cache_state",
            facet_col="cpu_mode",
            title="Peak RSS (kB) by spec / binary / cache state / CPU mode",
        )
        html["rss"] = plot_html(fig, include_plotlyjs=False, output_type="div")

    # Filesystem inputs as simple I/O proxy.
    fs_in_col = "File system inputs"
    if fs_in_col in df.columns:
        fig = px.box(
            df,
            x="spec",
            y=fs_in_col,
            color="binary",
            facet_row="cache_state",
            facet_col="cpu_mode",
            title="Filesystem inputs by spec / binary / cache state / CPU mode",
        )
        html["fs_inputs"] = plot_html(fig, include_plotlyjs=False, output_type="div")

    # IPC if present.
    if "ipc" in df.columns:
        fig = px.box(
            df,
            x="spec",
            y="ipc",
            color="binary",
            facet_row="cache_state",
            facet_col="cpu_mode",
            title="Instructions per cycle (IPC) by spec / binary / cache state / CPU mode",
        )
        html["ipc"] = plot_html(fig, include_plotlyjs=False, output_type="div")

    return html


def load_manifest(out_dir: Path) -> dict[str, object]:
    manifest_path = out_dir / "manifest.json"
    if not manifest_path.exists():
        return {}
    return json.loads(manifest_path.read_text())


def build_html_report(out_dir: Path, df: pd.DataFrame) -> str:
    manifest = load_manifest(out_dir)
    plots = make_plots(df)

    # Build aggregation spec only for columns that actually exist.
    agg_spec: dict[str, tuple[str, str]] = {}
    if "wall_time_s" in df.columns:
        agg_spec["wall_time_s_mean"] = ("wall_time_s", "mean")
        agg_spec["wall_time_s_std"] = ("wall_time_s", "std")
    if "Maximum resident set size (kbytes)" in df.columns:
        agg_spec["rss_kbytes_mean"] = ("Maximum resident set size (kbytes)", "mean")
    if "File system inputs" in df.columns:
        agg_spec["fs_inputs_mean"] = ("File system inputs", "mean")

    if agg_spec:
        summary_table = df.groupby(
            ["binary", "spec", "cache_state", "cpu_mode"],
        ).agg(**agg_spec)
        summary_html = summary_table.reset_index().to_html(
            index=False,
            float_format=lambda x: f"{x:.3f}",
        )
    else:
        summary_html = "<p>No numeric metrics available to summarize.</p>"

    manifest_html = (
        "<pre>" + json.dumps(manifest, indent=2) + "</pre>"
        if manifest
        else "<p>No manifest found.</p>"
    )

    # Build explicit wall-time tables when available.
    timing_comparison_html = ""
    per_run_timing_html = ""
    if "wall_time_s" in df.columns:
        g = (
            df.groupby(["spec", "cache_state", "cpu_mode", "binary"])["wall_time_s"]
            .mean()
            .reset_index()
            .pivot(
                index=["spec", "cache_state", "cpu_mode"],
                columns="binary",
                values="wall_time_s",
            )
        )
        cols = []
        if "old" in g.columns:
            g["old_s"] = g["old"]
            cols.append("old_s")
        if "new" in g.columns:
            g["new_s"] = g["new"]
            cols.append("new_s")
        if "old" in g.columns and "new" in g.columns:
            g["delta_s"] = g["new_s"] - g["old_s"]
            g["speedup"] = g["old_s"] / g["new_s"]
            cols.extend(["delta_s", "speedup"])

        if cols:
            timing_comparison_html = g.reset_index()[
                ["spec", "cache_state", "cpu_mode"] + cols
            ].to_html(index=False, float_format=lambda x: f"{x:.4f}")

        # Also expose a per-run wall-time table (capped to a reasonable size).
        per_run_timing_html = (
            df.sort_values(["spec", "cache_state", "cpu_mode", "binary", "run_id"])[
                ["binary", "spec", "cache_state", "cpu_mode", "run_id", "wall_time_s"]
            ]
            .head(200)
            .to_html(index=False, float_format=lambda x: f"{x:.4f}")
        )

    html_parts = [
        "<!DOCTYPE html>",
        "<html>",
        "<head>",
        "<meta charset='utf-8' />",
        "<title>Micromamba perf(1) Benchmark Report</title>",
        "<script src='https://cdn.plot.ly/plotly-latest.min.js'></script>",
        "<style>",
        "body { font-family: system-ui, -apple-system, BlinkMacSystemFont, "
        "'Segoe UI', sans-serif; margin: 1.5rem; }",
        "h1, h2, h3 { font-weight: 600; }",
        "section { margin-bottom: 2rem; }",
        "</style>",
        "</head>",
        "<body>",
        "<h1>Micromamba perf(1) Benchmark Report</h1>",
        "<section>",
        "<h2>Manifest</h2>",
        manifest_html,
        "</section>",
        "<section>",
        "<h2>Summary statistics</h2>",
        summary_html,
        "</section>",
    ]

    if timing_comparison_html:
        html_parts.extend(
            [
                "<section>",
                "<h2>Wall time comparison (seconds)</h2>",
                "<p>Per (spec, cache_state, cpu_mode) mean wall time for old/new, "
                "plus absolute delta and speedup ratio old/new.</p>",
                timing_comparison_html,
                "</section>",
            ]
        )
    if per_run_timing_html:
        html_parts.extend(
            [
                "<section>",
                "<h2>Per-run wall times (seconds, first 200 runs)</h2>",
                per_run_timing_html,
                "</section>",
            ]
        )

    if "wall_time" in plots:
        html_parts.extend(
            [
                "<section>",
                "<h2>Wall time</h2>",
                plots["wall_time"],
                "</section>",
            ]
        )
    if "rss" in plots:
        html_parts.extend(
            [
                "<section>",
                "<h2>Peak RSS</h2>",
                plots["rss"],
                "</section>",
            ]
        )
    if "fs_inputs" in plots:
        html_parts.extend(
            [
                "<section>",
                "<h2>Filesystem inputs</h2>",
                plots["fs_inputs"],
                "</section>",
            ]
        )
    if "ipc" in plots:
        html_parts.extend(
            [
                "<section>",
                "<h2>Instructions per cycle (IPC)</h2>",
                plots["ipc"],
                "</section>",
            ]
        )

    html_parts.extend(["</body>", "</html>"])
    return "\n".join(html_parts)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Aggregate micromamba perf(1) benchmark results and build HTML report."
    )
    p.add_argument(
        "--runs-dir",
        type=Path,
        default=Path("micromamba-perf-runs"),
        help="Directory containing perf outputs (manifest.json, perf-stat/, time/).",
    )
    p.add_argument(
        "--output-csv",
        type=Path,
        default=Path("aggregated.csv"),
        help="Path to write aggregated CSV.",
    )
    p.add_argument(
        "--output-html",
        type=Path,
        default=Path("report.html"),
        help="Path to write HTML report.",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    df = aggregate_runs(args.runs_dir)
    args.output_csv.write_text(df.to_csv(index=False))
    html = build_html_report(args.runs_dir, df)
    args.output_html.write_text(html)
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
