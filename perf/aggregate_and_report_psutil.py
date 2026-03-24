#!/usr/bin/env python3
"""
Aggregate psutil-based micromamba benchmark results and generate an HTML report.

Input:
  micromamba-psutil-runs/results.csv

Output:
  - aggregated_psutil.csv (optional, for debugging)
  - micromamba-psutil-report.html (or user-specified path)
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt  # type: ignore[import-untyped]
import pandas as pd  # type: ignore[import-untyped]
import random

BINARY_LABELS = {"old": "2.5.0", "new": "2.6.0"}
SPEC_DEPENDENCY_COUNTS = {
    "tzdata": 0,
    "xtensor": 5,
    "python": 21,
    "scikit-learn": 32,
    "pyarrow": 87,
    "jupyterlab": 140,
    "jupytergis": 166,
}


def binary_label(binary: str) -> str:
    return BINARY_LABELS.get(binary, binary)


def extract_unit(ylabel: str) -> str:
    start = ylabel.find("(")
    end = ylabel.find(")", start + 1)
    if start != -1 and end != -1 and end > start + 1:
        return ylabel[start + 1 : end].strip()
    return ylabel.strip()


def spec_sort_key(spec: str) -> tuple[int, str]:
    # Keep unknown specs after known ones, ordered alphabetically.
    return (SPEC_DEPENDENCY_COUNTS.get(spec, 10**9), spec)


def sort_by_spec_and_cache_state(df: pd.DataFrame) -> pd.DataFrame:
    if "spec" not in df.columns:
        return df
    ordered_specs = sorted(df["spec"].astype(str).unique().tolist(), key=spec_sort_key)
    ordered_df = df.copy()
    ordered_df["spec"] = pd.Categorical(
        ordered_df["spec"],
        categories=ordered_specs,
        ordered=True,
    )
    sort_cols = ["spec"]
    if "cache_state" in ordered_df.columns:
        sort_cols.append("cache_state")
    return ordered_df.sort_values(sort_cols).reset_index(drop=True)


def df_to_markdown(df: pd.DataFrame) -> str:
    """
    Render a DataFrame as a GitHub-style Markdown table:
    | col1 | col2 |
    | ---- | ---- |
    | ...  | ...  |
    """
    if df.empty:
        return ""

    # Column headers
    headers = list(df.columns)
    # Rename spec column to "Specification" in Markdown tables
    md_headers = ["Specification" if str(h).lower() == "spec" else str(h) for h in headers]
    header_row = "| " + " | ".join(md_headers) + " |"

    # Right-align numeric columns by using '---:' in the separator row
    separators: list[str] = []
    for h in headers:
        col_name = str(h)
        col = df[col_name]
        lower = col_name.lower()
        # Center-align specification and cache-state columns
        if lower in ("spec", "specification", "cache state"):
            separators.append(":---:")
        # Right-align numeric columns
        elif pd.api.types.is_numeric_dtype(col):
            separators.append("---:")
        else:
            separators.append("---")
    separator_row = "| " + " | ".join(separators) + " |"

    body_rows: list[str] = []
    for _, row in df.iterrows():
        cells: list[str] = []
        for col, val in row.items():
            if isinstance(val, float):
                cell = f"{val:.4f}"
            else:
                cell = str(val)
            # Quote specification values for clarity
            col_lower = str(col).lower()
            if col_lower in ("spec", "specification"):
                cell = f"`{cell}`"
            # Bold speedup / ratio / reduction columns for emphasis
            if "speedup" in col_lower or "ratio" in col_lower or "reduction" in col_lower:
                cell = f"**{cell}**"
            cells.append(cell)
        body_rows.append("| " + " | ".join(cells) + " |")

    return "\n".join([header_row, separator_row, *body_rows])


def load_results(csv_path: Path) -> pd.DataFrame:
    if not csv_path.exists():
        raise SystemExit(f"Results CSV not found: {csv_path}")
    df = pd.read_csv(csv_path)
    return df


def generate_boxplot_pngs_by_state(
    df: pd.DataFrame,
    value_col: str,
    ylabel: str,
    title_prefix: str,
    png_basename: str,
    out_dir: Path,
) -> dict[str, str]:
    """
    Generate separate high-definition boxplots: one image per cache_state.
    x-axis: project; for each project, two boxes (2.5.0/2.6.0) when both exist.
    """
    # Okabe-Ito colorblind-safe palette for version encoding.
    version_colors = {
        "2.5.0": "#0072B2",  # blue
        "2.6.0": "#E69F00",  # orange
    }
    default_color = "#009E73"  # green

    images: dict[str, str] = {}
    cache_states = sorted(df["cache_state"].unique().tolist())

    for cache_state in cache_states:
        df_sub = df[df["cache_state"] == cache_state].copy()
        if df_sub.empty:
            continue

        specs = sorted(df_sub["spec"].unique().tolist(), key=spec_sort_key)
        binaries = sorted(df_sub["binary"].unique().tolist())

        data: list[list[float]] = []
        labels: list[str] = []
        stats_labels: list[str] = []
        box_colors: list[str] = []
        unit = extract_unit(ylabel)

        for spec in specs:
            for binary in binaries:
                vals = (
                    df_sub[(df_sub["spec"] == spec) & (df_sub["binary"] == binary)][value_col]
                    .astype(float)
                    .tolist()
                )
                if not vals:
                    continue
                data.append(vals)
                version = binary_label(binary)
                labels.append(f"{spec} @ {version}")
                box_colors.append(version_colors.get(version, default_color))
                vals_series = pd.Series(vals, dtype=float)
                vmin = float(vals_series.min())
                q1 = float(vals_series.quantile(0.25))
                mean = float(vals_series.mean())
                median = float(vals_series.median())
                q3 = float(vals_series.quantile(0.75))
                vmax = float(vals_series.max())
                stats_labels.append(
                    (
                        f"Min: {vmin:.2f} {unit}\n"
                        f"Q1: {q1:.2f} {unit}\n"
                        f"Mean: {mean:.2f} {unit}\n"
                        f"Median: {median:.2f} {unit}\n"
                        f"Q3: {q3:.2f} {unit}\n"
                        f"Max: {vmax:.2f} {unit}"
                    ),
                )

        if not data:
            continue

        png_name = f"{png_basename}_{cache_state}.png"
        png_path = out_dir / png_name
        png_path.parent.mkdir(parents=True, exist_ok=True)

        # Use a large 16:9 canvas to keep labels readable in reports.
        width = max(16.0, len(labels) * 0.9)
        height = width * 9.0 / 16.0
        plt.figure(figsize=(width, height), dpi=220)
        bp = plt.boxplot(data, labels=labels, showfliers=True, patch_artist=True)

        # Style boxes/lines for readability and accessibility.
        for patch, color in zip(bp["boxes"], box_colors):
            patch.set_facecolor(color)
            patch.set_edgecolor("#1A1A1A")
            patch.set_alpha(0.55)
            patch.set_linewidth(1.2)
        for median in bp["medians"]:
            median.set_color("#1A1A1A")
            median.set_linewidth(1.6)
        for whisker in bp["whiskers"]:
            whisker.set_color("#1A1A1A")
            whisker.set_linewidth(1.1)
        for cap in bp["caps"]:
            cap.set_color("#1A1A1A")
            cap.set_linewidth(1.1)
        for flier in bp["fliers"]:
            flier.set_markerfacecolor("#1A1A1A")
            flier.set_markeredgecolor("#1A1A1A")
            flier.set_alpha(0.45)

        # Overlay jittered scatter points for each run in each group.
        for x_pos, vals in enumerate(data, start=1):
            for v in vals:
                jitter = (random.random() - 0.5) * 0.15  # small horizontal jitter
                plt.scatter(
                    x_pos + jitter,
                    v,
                    color=box_colors[x_pos - 1],
                    edgecolors="#1A1A1A",
                    alpha=0.75,
                    s=12,
                    linewidths=0.3,
                )

        # Annotate stats near each box (next to its upper whisker).
        whiskers = bp.get("whiskers", [])
        for idx, stat_text in enumerate(stats_labels):
            whisker_i = idx * 2 + 1
            if whisker_i < len(whiskers):
                y_vals = whiskers[whisker_i].get_ydata()
                y_anchor = float(max(y_vals)) if len(y_vals) > 0 else max(data[idx])
            else:
                y_anchor = max(data[idx])
            plt.annotate(
                stat_text,
                xy=(idx + 1, y_anchor),
                xytext=(-6, 6),
                textcoords="offset points",
                ha="right",
                va="bottom",
                fontsize=8,
                color="black",
                bbox={
                    "boxstyle": "round,pad=0.2",
                    "facecolor": "white",
                    "alpha": 0.75,
                    "edgecolor": "0.8",
                },
            )

        plt.xticks(rotation=45, ha="right")
        plt.ylabel(ylabel)
        runs_per_box = sorted({len(vals) for vals in data})
        if len(runs_per_box) == 1:
            n_text = str(runs_per_box[0])
        else:
            n_text = f"{runs_per_box[0]}-{runs_per_box[-1]}"
        plt.title(f"{title_prefix} – cache={cache_state} (n={n_text} per box)")
        # Keep both axes starting at 0 for visual consistency across plots.
        plt.xlim(left=0)
        max_observed = max(max(vals) for vals in data)
        y_top = max_observed * 1.3 if max_observed > 0 else 1.0
        plt.ylim(bottom=0, top=y_top)
        plt.tight_layout()
        plt.savefig(png_path, dpi=300, bbox_inches="tight")
        plt.close()

        images[cache_state] = png_name

    return images


def build_comparison_table_html(
    df: pd.DataFrame,
    ratio_col: str,
    headers: list[str],
) -> str:
    """
    Build an HTML table with verbose headers and a green background gradient
    on the ratio/reduction column (higher values -> greener cells).
    """
    cols = df.columns.tolist()
    if len(headers) != len(cols):
        # Fallback: simple to_html if headers do not match.
        return df.to_html(index=False, float_format=lambda x: f"{x:.4f}")

    max_ratio = df[ratio_col].max()
    min_ratio = df[ratio_col].min()
    denom = float(max_ratio - min_ratio) if pd.notna(max_ratio) else 0.0

    parts: list[str] = []
    parts.append("<table border='1' class='dataframe'>")
    parts.append("<thead><tr>")
    for h in headers:
        parts.append(f"<th>{h}</th>")
    parts.append("</tr></thead>")
    parts.append("<tbody>")

    for _, row in df.iterrows():
        parts.append("<tr>")
        for col in cols:
            val = row[col]
            text = f"{val:.4f}" if isinstance(val, (float, int)) else str(val)
            style = ""
            if col == ratio_col and denom > 0:
                r = float(val)
                rel = (r - min_ratio) / denom if denom > 0 else 0.0
                rel = max(0.0, min(1.0, rel))
                # Interpolate between white and target green:
                # Min: rgb(255,255,255), Max: rgb(25,120,71)
                r_col = int(255 + (25 - 255) * rel)
                g_col = int(255 + (120 - 255) * rel)
                b_col = int(255 + (71 - 255) * rel)
                style = f" style='background-color: rgb({r_col},{g_col},{b_col});'"
                text = f"<strong>{text}</strong>"
            parts.append(f"<td{style}>{text}</td>")
        parts.append("</tr>")
    parts.append("</tbody></table>")
    return "\n".join(parts)


def build_html_report(df: pd.DataFrame, html_path: Path) -> str:
    plots_dir = html_path.parent

    # Markdown snippets for parallel .md report
    summary_md = ""
    wall_md = ""
    rss_md = ""
    net_io_md = ""
    deps_md = ""

    # Summary statistics per (binary, spec, cache_state).
    agg_spec: dict[str, tuple[str, str]] = {}
    if "wall_time_s" in df.columns:
        agg_spec["wall_time_s_mean"] = ("wall_time_s", "mean")
        agg_spec["wall_time_s_std"] = ("wall_time_s", "std")
    if "peak_rss_bytes" in df.columns:
        agg_spec["peak_rss_bytes_mean"] = ("peak_rss_bytes", "mean")
        agg_spec["peak_rss_bytes_std"] = ("peak_rss_bytes", "std")
    if "net_bytes_sent" in df.columns:
        agg_spec["net_bytes_sent_mean"] = ("net_bytes_sent", "mean")
        agg_spec["net_bytes_sent_std"] = ("net_bytes_sent", "std")
    if "net_bytes_recv" in df.columns:
        agg_spec["net_bytes_recv_mean"] = ("net_bytes_recv", "mean")
        agg_spec["net_bytes_recv_std"] = ("net_bytes_recv", "std")

    if agg_spec:
        summary_table = df.groupby(
            ["binary", "spec", "cache_state"],
        ).agg(**agg_spec)
        summary_df = summary_table.reset_index()
        summary_df = sort_by_spec_and_cache_state(summary_df)
        if "peak_rss_bytes_mean" in summary_df.columns:
            summary_df["peak_rss_mib_mean"] = summary_df["peak_rss_bytes_mean"] / (1024 * 1024)
        if "net_bytes_sent_mean" in summary_df.columns:
            summary_df["net_sent_mib_mean"] = summary_df["net_bytes_sent_mean"] / (1024 * 1024)
        if "net_bytes_recv_mean" in summary_df.columns:
            summary_df["net_recv_mib_mean"] = summary_df["net_bytes_recv_mean"] / (1024 * 1024)
        summary_html = summary_df.to_html(
            index=False,
            float_format=lambda x: f"{x:.4f}",
        )
        summary_md = df_to_markdown(summary_df)
    else:
        summary_html = "<p>No numeric metrics available to summarize.</p>"

    # 2.5.0 vs 2.6.0 comparison tables (time and memory).
    wall_cmp_html = ""
    rss_cmp_html = ""
    wall_tables_html: dict[str, str] = {}
    wall_tables_md: dict[str, str] = {}
    rss_tables_html: dict[str, str] = {}
    rss_tables_md: dict[str, str] = {}
    net_tables_html: dict[str, str] = {}
    net_tables_md: dict[str, str] = {}
    if "wall_time_s" in df.columns:
        g = (
            df.groupby(["spec", "cache_state", "binary"])["wall_time_s"]
            .mean()
            .reset_index()
            .pivot(index=["spec", "cache_state"], columns="binary", values="wall_time_s")
        )
        cols: list[str] = []
        if "old" in g.columns:
            g["old_wall_s"] = g["old"]
            cols.append("old_wall_s")
        if "new" in g.columns:
            g["new_wall_s"] = g["new"]
            cols.append("new_wall_s")
        if {"old", "new"}.issubset(set(g.columns)):
            g["delta_wall_s"] = g["new_wall_s"] - g["old_wall_s"]
            g["speedup"] = g["old_wall_s"] / g["new_wall_s"]
            cols.extend(["delta_wall_s", "speedup"])
        if cols:
            wall_df = g.reset_index()[["spec", "cache_state"] + cols]
            wall_df = sort_by_spec_and_cache_state(wall_df)
            wall_cmp_html = build_comparison_table_html(
                wall_df,
                ratio_col="speedup",
                headers=[
                    "Specification",
                    "Cache state",
                    "2.5.0 wall time (s)",
                    "2.6.0 wall time (s)",
                    "Delta wall time (s)",
                    "Speedup",
                ],
            )
            wall_md_df = wall_df.rename(
                columns={
                    "spec": "Specification",
                    "cache_state": "Cache state",
                    "old_wall_s": "2.5.0 wall time (s)",
                    "new_wall_s": "2.6.0 wall time (s)",
                    "delta_wall_s": "Delta wall time (s)",
                    "speedup": "Speedup",
                },
            )
            wall_md = df_to_markdown(wall_md_df)
            for state in ["cold", "warm"]:
                state_df = wall_df[wall_df["cache_state"] == state].copy()
                if state_df.empty:
                    continue
                state_df = state_df.drop(columns=["cache_state"])
                wall_tables_html[state] = build_comparison_table_html(
                    state_df,
                    ratio_col="speedup",
                    headers=[
                        "Specification",
                        "2.5.0 wall time (s)",
                        "2.6.0 wall time (s)",
                        "Delta wall time (s)",
                        "Speedup",
                    ],
                )
                wall_tables_md[state] = df_to_markdown(
                    state_df.rename(
                        columns={
                            "spec": "Specification",
                            "old_wall_s": "2.5.0 wall time (s)",
                            "new_wall_s": "2.6.0 wall time (s)",
                            "delta_wall_s": "Delta wall time (s)",
                            "speedup": "Speedup",
                        },
                    ),
                )

    if "peak_rss_bytes" in df.columns:
        h = (
            df.groupby(["spec", "cache_state", "binary"])["peak_rss_bytes"]
            .mean()
            .reset_index()
            .pivot(index=["spec", "cache_state"], columns="binary", values="peak_rss_bytes")
        )
        cols_rss: list[str] = []
        if "old" in h.columns:
            h["old_rss_mib"] = h["old"] / (1024 * 1024)
            cols_rss.append("old_rss_mib")
        if "new" in h.columns:
            h["new_rss_mib"] = h["new"] / (1024 * 1024)
            cols_rss.append("new_rss_mib")
        if {"old", "new"}.issubset(set(h.columns)):
            h["delta_rss_mib"] = h["new_rss_mib"] - h["old_rss_mib"]
            h["rss_ratio_old_new"] = h["old_rss_mib"] / h["new_rss_mib"]
            cols_rss.extend(["delta_rss_mib", "rss_ratio_old_new"])
        if cols_rss:
            rss_df = h.reset_index()[["spec", "cache_state"] + cols_rss]
            rss_df = sort_by_spec_and_cache_state(rss_df)
            rss_cmp_html = build_comparison_table_html(
                rss_df,
                ratio_col="rss_ratio_old_new",
                headers=[
                    "Specification",
                    "Cache state",
                    "2.5.0 peak RSS (MiB)",
                    "2.6.0 peak RSS (MiB)",
                    "Delta peak RSS (MiB)",
                    "Peak RSS reduction",
                ],
            )
            rss_md_df = rss_df.rename(
                columns={
                    "spec": "Specification",
                    "cache_state": "Cache state",
                    "old_rss_mib": "2.5.0 peak RSS (MiB)",
                    "new_rss_mib": "2.6.0 peak RSS (MiB)",
                    "delta_rss_mib": "Delta peak RSS (MiB)",
                    "rss_ratio_old_new": "Peak RSS reduction",
                },
            )
            rss_md = df_to_markdown(rss_md_df)
            for state in ["cold", "warm"]:
                state_df = rss_df[rss_df["cache_state"] == state].copy()
                if state_df.empty:
                    continue
                state_df = state_df.drop(columns=["cache_state"])
                rss_tables_html[state] = build_comparison_table_html(
                    state_df,
                    ratio_col="rss_ratio_old_new",
                    headers=[
                        "Specification",
                        "2.5.0 peak RSS (MiB)",
                        "2.6.0 peak RSS (MiB)",
                        "Delta peak RSS (MiB)",
                        "Peak RSS reduction",
                    ],
                )
                rss_tables_md[state] = df_to_markdown(
                    state_df.rename(
                        columns={
                            "spec": "Specification",
                            "old_rss_mib": "2.5.0 peak RSS (MiB)",
                            "new_rss_mib": "2.6.0 peak RSS (MiB)",
                            "delta_rss_mib": "Delta peak RSS (MiB)",
                            "rss_ratio_old_new": "Peak RSS reduction",
                        },
                    ),
                )

    # Network I/O comparison table (sent+received).
    net_io_cmp_html = ""
    if "net_bytes_sent" in df.columns and "net_bytes_recv" in df.columns:
        # Network I/O is only meaningful for cold-cache runs.
        df_net_cold = df[df["cache_state"] == "cold"].copy()
        ns = (
            df_net_cold.groupby(["spec", "cache_state", "binary"])
            .agg(
                net_bytes_sent=("net_bytes_sent", "mean"),
                net_bytes_recv=("net_bytes_recv", "mean"),
            )
            .reset_index()
        )
        # Total network I/O in MiB (sent + received).
        ns["net_io_mib"] = (ns["net_bytes_sent"] + ns["net_bytes_recv"]) / (1024 * 1024)
        pivot_io = ns.pivot(
            index=["spec", "cache_state"],
            columns="binary",
            values="net_io_mib",
        )
        if {"old", "new"}.issubset(set(pivot_io.columns)):
            t = pivot_io.copy()
            t["old_io_mib"] = t["old"]
            t["new_io_mib"] = t["new"]
            t["delta_io_mib"] = t["new_io_mib"] - t["old_io_mib"]
            t["ratio_io_old_new"] = t["old_io_mib"] / t["new_io_mib"]
            io_df = t.reset_index()[
                [
                    "spec",
                    "cache_state",
                    "old_io_mib",
                    "new_io_mib",
                    "delta_io_mib",
                    "ratio_io_old_new",
                ]
            ]
            io_df = sort_by_spec_and_cache_state(io_df)
            net_io_cmp_html = build_comparison_table_html(
                io_df,
                ratio_col="ratio_io_old_new",
                headers=[
                    "Specification",
                    "Cache state",
                    "2.5.0 network I/O (MiB)",
                    "2.6.0 network I/O (MiB)",
                    "Delta network I/O (MiB)",
                    "Network I/O reduction",
                ],
            )
            net_io_md_df = io_df.rename(
                columns={
                    "spec": "Specification",
                    "cache_state": "Cache state",
                    "old_io_mib": "2.5.0 network I/O (MiB)",
                    "new_io_mib": "2.6.0 network I/O (MiB)",
                    "delta_io_mib": "Delta network I/O (MiB)",
                    "ratio_io_old_new": "Network I/O reduction",
                },
            )
            net_io_md = df_to_markdown(net_io_md_df)
            for state in ["cold", "warm"]:
                state_df = io_df[io_df["cache_state"] == state].copy()
                if state_df.empty:
                    continue
                state_df = state_df.drop(columns=["cache_state"])
                net_tables_html[state] = build_comparison_table_html(
                    state_df,
                    ratio_col="ratio_io_old_new",
                    headers=[
                        "Specification",
                        "2.5.0 network I/O (MiB)",
                        "2.6.0 network I/O (MiB)",
                        "Delta network I/O (MiB)",
                        "Network I/O reduction",
                    ],
                )
                net_tables_md[state] = df_to_markdown(
                    state_df.rename(
                        columns={
                            "spec": "Specification",
                            "old_io_mib": "2.5.0 network I/O (MiB)",
                            "new_io_mib": "2.6.0 network I/O (MiB)",
                            "delta_io_mib": "Delta network I/O (MiB)",
                            "ratio_io_old_new": "Network I/O reduction",
                        },
                    ),
                )

    # Simple manifest-like info derived from the data.
    meta = {
        "micromamba_versions": sorted(
            [binary_label(b) for b in df["binary"].unique().tolist()],
        ),
        "specs": sorted(df["spec"].unique().tolist(), key=spec_sort_key),
        "cache_states": sorted(df["cache_state"].unique().tolist()),
        "runs_per_combo": int(
            df.groupby(["binary", "spec", "cache_state"])["run_id"].nunique().max(),
        ),
    }
    manifest_html = "<pre>" + json.dumps(meta, indent=2) + "</pre>"

    # Prepare plot images so they can be embedded under metric sections.
    wall_images: dict[str, str] = {}
    rss_images: dict[str, str] = {}
    net_images: dict[str, str] = {}
    if "wall_time_s" in df.columns:
        wall_images = generate_boxplot_pngs_by_state(
            df,
            value_col="wall_time_s",
            ylabel="Wall time (s)",
            title_prefix="Wall time per project installation and micromamba version",
            png_basename="wall_time_boxplot",
            out_dir=plots_dir,
        )
    if "peak_rss_bytes" in df.columns:
        df_rss = df.copy()
        df_rss["peak_rss_mib"] = df_rss["peak_rss_bytes"] / (1024 * 1024)
        rss_images = generate_boxplot_pngs_by_state(
            df_rss,
            value_col="peak_rss_mib",
            ylabel="Peak RSS (MiB)",
            title_prefix="Peak RSS per project installation and micromamba version",
            png_basename="peak_rss_boxplot",
            out_dir=plots_dir,
        )
    if "net_bytes_sent" in df.columns and "net_bytes_recv" in df.columns:
        # Exclude warm-cache network I/O from plots.
        df_net = df[df["cache_state"] == "cold"].copy()
        df_net["net_io_mib"] = (df_net["net_bytes_sent"] + df_net["net_bytes_recv"]) / (1024 * 1024)
        net_images = generate_boxplot_pngs_by_state(
            df_net,
            value_col="net_io_mib",
            ylabel="Network I/O (MiB)",
            title_prefix="Network I/O per project installation and micromamba version",
            png_basename="net_io_boxplot",
            out_dir=plots_dir,
        )

    html_parts: list[str] = [
        "<!DOCTYPE html>",
        "<html>",
        "<head>",
        "<meta charset='utf-8' />",
        "<title>Micromamba psutil Benchmark Report</title>",
        "<style>",
        "body { font-family: system-ui, -apple-system, BlinkMacSystemFont, "
        "'Segoe UI', sans-serif; margin: 1.5rem; }",
        "h1, h2, h3 { font-weight: 600; }",
        "section { margin-bottom: 2rem; }",
        "table { border-collapse: collapse; width: 100%; max-width: 100%; }",
        "table th, table td { padding: 0.35rem 0.5rem; }",
        "section { overflow-x: auto; }",
        "img {",
        "  display: block;",
        "  width: min(100%, 1800px);",
        "  max-width: 100%;",
        "  height: auto;",
        "  margin: 0.75rem 0 1.25rem 0;",
        "  border: 1px solid #d0d7de;",
        "  border-radius: 8px;",
        "  background: #fff;",
        "}",
        "</style>",
        "</head>",
        "<body>",
        "<h1>Micromamba psutil Benchmark Report</h1>",
        "<section>",
        "<h2>Overview</h2>",
        "<p>Metrics collected using psutil (wall time, peak RSS, network I/O) "
        "for micromamba dry-run installs (multithreaded).</p>",
        "</section>",
        "<section>",
        "<h2>Manifest</h2>",
        manifest_html,
        "</section>",
    ]

    if wall_cmp_html:
        html_parts.extend(
            [
                "<section>",
                "<h2>Wall time comparison (seconds)</h2>",
                "<p>Per (project, cache_state) mean wall time for "
                "2.5.0/2.6.0, plus absolute delta and speedup ratio "
                "2.5.0/2.6.0 (when both versions are present).</p>",
            ]
        )
        for state in ["cold", "warm"]:
            if state in wall_tables_html:
                html_parts.append(f"<h3>{state.capitalize()} cache</h3>")
                html_parts.append(wall_tables_html[state])
        if wall_images:
            html_parts.append("<h3>Wall time distributions (boxplots)</h3>")
            for cache_state, fname in sorted(wall_images.items()):
                html_parts.append(f"<h4>Cache: {cache_state}</h4>")
                html_parts.append(
                    f"<img src='{fname}' alt='Wall time boxplot {cache_state}' />",
                )
        html_parts.append("</section>")
    if rss_cmp_html:
        html_parts.extend(
            [
                "<section>",
                "<h2>Peak RSS comparison (MiB)</h2>",
                "<p>Per (project, cache_state) mean peak RSS for "
                "2.5.0/2.6.0, plus delta when both versions are present.</p>",
            ]
        )
        for state in ["cold", "warm"]:
            if state in rss_tables_html:
                html_parts.append(f"<h3>{state.capitalize()} cache</h3>")
                html_parts.append(rss_tables_html[state])
        if rss_images:
            html_parts.append("<h3>Peak RSS distributions (boxplots)</h3>")
            for cache_state, fname in sorted(rss_images.items()):
                html_parts.append(f"<h4>Cache: {cache_state}</h4>")
                html_parts.append(
                    f"<img src='{fname}' alt='Peak RSS boxplot {cache_state}' />",
                )
        html_parts.append("</section>")
    if net_io_cmp_html:
        html_parts.extend(
            [
                "<section>",
                "<h2>Network I/O comparison (MiB)</h2>",
                "<p>💡 Comparison on warm cache aren't reported because "
                "network is not used in this case.</p>",
                "<p>Per (project, cache_state) mean network bytes (sent + "
                "received, system-wide during run) for 2.5.0/2.6.0. "
                "Only cold-cache runs are shown.</p>",
            ]
        )
        if "cold" in net_tables_html:
            html_parts.append("<h3>Cold cache</h3>")
            html_parts.append(net_tables_html["cold"])
        html_parts.append("<h3>Warm cache</h3>")
        html_parts.append("<p>Not reported.</p>")
        if net_images:
            html_parts.append("<h3>Network I/O distributions (boxplots)</h3>")
            for cache_state, fname in sorted(net_images.items()):
                html_parts.append(f"<h4>Cache: {cache_state}</h4>")
                html_parts.append(
                    f"<img src='{fname}' alt='Network I/O boxplot {cache_state}' />",
                )
        html_parts.append("</section>")

    # Dependency counts (hard-coded per spec)
    deps_df = pd.DataFrame(
        [
            {"Spec": spec, "Dependencies": deps}
            for spec, deps in sorted(SPEC_DEPENDENCY_COUNTS.items(), key=lambda x: x[1])
        ]
    )
    deps_md = df_to_markdown(deps_df)

    # Write parallel Markdown report
    md_path = html_path.with_suffix(".md")
    md_sections: list[str] = ["# Micromamba psutil Benchmark Report"]
    md_sections.append("## Manifest")
    md_sections.append("```")
    md_sections.append(json.dumps(meta, indent=2))
    md_sections.append("```")
    md_sections.append("## Dependency counts per spec")
    md_sections.append(deps_md)
    if summary_md:
        md_sections.append("## Summary statistics")
        md_sections.append(summary_md)
    if wall_md:
        md_sections.append("## Wall time comparison (seconds)")
        if "cold" in wall_tables_md:
            md_sections.append("### Cold cache")
            md_sections.append(wall_tables_md["cold"])
        if "warm" in wall_tables_md:
            md_sections.append("### Warm cache")
            md_sections.append(wall_tables_md["warm"])
        if wall_images:
            md_sections.append("### Wall time distributions (boxplots)")
            for cache_state, fname in sorted(wall_images.items()):
                md_sections.append(f"#### Cache: {cache_state}")
                md_sections.append(f"![Wall time boxplot {cache_state}]({fname})")
    if rss_md:
        md_sections.append("## Peak RSS comparison (MiB)")
        if "cold" in rss_tables_md:
            md_sections.append("### Cold cache")
            md_sections.append(rss_tables_md["cold"])
        if "warm" in rss_tables_md:
            md_sections.append("### Warm cache")
            md_sections.append(rss_tables_md["warm"])
        if rss_images:
            md_sections.append("### Peak RSS distributions (boxplots)")
            for cache_state, fname in sorted(rss_images.items()):
                md_sections.append(f"#### Cache: {cache_state}")
                md_sections.append(f"![Peak RSS boxplot {cache_state}]({fname})")
    if net_io_md:
        md_sections.append("## Network I/O comparison (MiB)")
        md_sections.append(
            "💡 Comparison on warm cache aren't reported because network is not used in this case.",
        )
        if "cold" in net_tables_md:
            md_sections.append("### Cold cache")
            md_sections.append(net_tables_md["cold"])
        md_sections.append("### Warm cache")
        md_sections.append("Not reported.")
        if net_images:
            md_sections.append("### Network I/O distributions (boxplots)")
            for cache_state, fname in sorted(net_images.items()):
                md_sections.append(f"#### Cache: {cache_state}")
                md_sections.append(f"![Network I/O boxplot {cache_state}]({fname})")
    md_path.write_text("\n\n".join(md_sections))

    html_parts.extend(
        [
            "<section>",
            "<h2>Summary statistics</h2>",
            summary_html,
            "</section>",
            "</body>",
            "</html>",
        ]
    )
    return "\n".join(html_parts)


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Aggregate psutil-based micromamba benchmark results and build HTML report.",
    )
    p.add_argument(
        "--runs-dir",
        type=Path,
        default=Path("micromamba-psutil-runs"),
        help="Directory containing results.csv.",
    )
    p.add_argument(
        "--input-csv",
        type=Path,
        default=None,
        help="Path to results.csv (defaults to runs-dir/results.csv).",
    )
    p.add_argument(
        "--output-csv",
        type=Path,
        default=Path("aggregated_psutil.csv"),
        help="Path to write a copy of the aggregated CSV.",
    )
    p.add_argument(
        "--output-html",
        type=Path,
        default=Path("micromamba-psutil-report.html"),
        help="Path to write HTML report.",
    )
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    csv_path = args.input_csv or (args.runs_dir / "results.csv")
    df = load_results(csv_path)
    # Write a copy for easy inspection.
    args.output_csv.write_text(df.to_csv(index=False))
    html = build_html_report(df, args.output_html)
    args.output_html.write_text(html)
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv[1:]))
