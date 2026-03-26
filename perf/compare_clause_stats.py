#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path


RULE_RE = re.compile(
    r"rule_stats (?P<phase>\S+) .* rules=(?P<rules>\d+) .* "
    r"provider_literals=(?P<provider>\d+) approx_literals=(?P<approx>\d+)"
)
MEM_RE = re.compile(r"overall rule memory used: (?P<kib>\d+) K")
SHARD_RE = re.compile(
    r"Processing fetched shard for package '(?P<pkg>[^']+)': "
    r"(?P<tbz2>\d+) \.tar\.bz2 packages, (?P<conda>\d+) \.conda packages"
)


def parse_log(path: Path, package_name: str | None = None) -> dict[str, int]:
    out: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = RULE_RE.search(line)
        if m:
            phase = m.group("phase")
            out[f"{phase}.rules"] = int(m.group("rules"))
            out[f"{phase}.provider_literals"] = int(m.group("provider"))
            out[f"{phase}.approx_literals"] = int(m.group("approx"))
            continue
        m = MEM_RE.search(line)
        if m:
            out["rule_memory_kib"] = int(m.group("kib"))
            continue
        m = SHARD_RE.search(line)
        if m:
            if package_name is not None and m.group("pkg") != package_name:
                continue
            out["shard_tbz2"] = int(m.group("tbz2"))
            out["shard_conda"] = int(m.group("conda"))
            out["shard_total"] = out["shard_tbz2"] + out["shard_conda"]
    return out


def get(d: dict[str, int], key: str) -> int:
    return int(d.get(key, 0))


def ratio(a: int, b: int) -> str:
    if b == 0:
        return "inf"
    return f"{a / b:.2f}x"


def main() -> int:
    p = argparse.ArgumentParser(
        description="Compare libsolv rule/clause stats from two stderr logs."
    )
    p.add_argument("--a-name", required=True)
    p.add_argument("--a-log", type=Path, required=True)
    p.add_argument(
        "--a-package", default=None, help="Requested package name in log (default: a-name)."
    )
    p.add_argument("--b-name", required=True)
    p.add_argument("--b-log", type=Path, required=True)
    p.add_argument(
        "--b-package", default=None, help="Requested package name in log (default: b-name)."
    )
    args = p.parse_args()

    a_package = args.a_package or args.a_name
    b_package = args.b_package or args.b_name
    a = parse_log(args.a_log, package_name=a_package)
    b = parse_log(args.b_log, package_name=b_package)

    keys = [
        "shard_total",
        "pkg_rules.job_involved.rules",
        "pkg_rules.total_post_unify.rules",
        "pkg_rules.total_post_unify.provider_literals",
        "pkg_rules.total_post_unify.approx_literals",
        "job_rules.provider_literals",
        "rule_memory_kib",
    ]

    print(f"Comparison: {args.a_name} vs {args.b_name}")
    print()
    for k in keys:
        av = get(a, k)
        bv = get(b, k)
        print(f"{k}: {args.a_name}={av}  {args.b_name}={bv}  ratio={ratio(av, bv)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
