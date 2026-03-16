# Micromamba perf(1) Benchmark Suite

This directory contains a small benchmarking harness to compare two `micromamba`
binaries using `perf(1)` and `/usr/bin/time -v` on dry-run installs.

Two binaries are compared:

- `MICROMAMBA_OLD`: path to the reference binary.
- `MICROMAMBA_NEW`: path to the candidate binary.

Both binaries must support `--version`; the harness records their versions in
the run manifest.

Configuration is taken from your normal Conda/Mamba setup:

- `MAMBA_ROOT_PREFIX` is set from the `--root-prefix` argument.
- If `CONDARC` is not already set in the environment and `~/.condarc` exists,
  it is automatically exported so both binaries use the same configuration.

The harness measures:

- Wall-clock time and CPU counters (`perf stat`).
- Peak RSS and filesystem I/O counters (`/usr/bin/time -v`).
- Optional instruction-level profiles (`perf record`).

Workloads are independent dry-run installs of:

- `xtensor`
- `python`
- `jupyterlab`
- `pyarrow`
- `skore`

Each workload is run for both:

- CPU modes: single-core (pinned with `taskset -c 0`) and multi-core (no affinity).
- Cache states: cold (caches cleared before the series) and warm (caches reused).

See `run_micromamba_perf.py` for CLI options and workflow details.
