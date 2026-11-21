#!/usr/bin/env python3
"""Generate an evidence-checked, non-synthetic Base-NEXS syscall fuzz report."""
from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATHS = ("direct", "wire", "REG_IPC")
CSV_DEFAULT = ROOT / "build/assurance/system_syscall_fuzz_1m_3paths.csv"
LOG_DEFAULT = ROOT / "build/assurance/system_fuzz_1m_3paths.log"
OUTPUT_DEFAULT = ROOT / "docs/rearchitecture/P8_SYSCALL_PROFILE_2026_11_21.md"


def syscall_verbs():
    source = (ROOT / "kernel/syscall.c").read_text()
    match = re.search(r"static const SyscallEntry s_table\[\] = \{(.*?)\};",
                      source, re.S)
    if not match:
        raise RuntimeError("cannot enumerate actual production kernel verbs")
    verbs = re.findall(r'\{\s*"([a-z]+)"\s*,\s*sys_[a-z]+\s*\}',
                       match.group(1))
    if len(verbs) == 0 or len(verbs) != len(set(verbs)):
        raise RuntimeError("invalid kernel syscall dispatch table")
    return verbs


def read_metrics(path):
    with path.open(newline="") as handle:
        reader = csv.DictReader(
            line for line in handle if not line.lstrip().startswith("#"))
        rows = list(reader)
    if not rows:
        raise RuntimeError("empty real metrics")
    metrics = {}
    for row in rows:
        name = row["operation"]
        if name in metrics:
            raise RuntimeError(f"duplicate metric: {name}")
        if int(row["calls"]) < 1 or int(row["assertion_failures"]) != 0:
            raise RuntimeError(f"unreliable or failing evidence: {name}")
        metrics[name] = row
    return metrics


def read_run(log):
    text = log.read_text()
    match = re.search(r"seed=(\d+) random_cases=(\d+) verbs=(\d+)"
                      r" direct\+wire\+IPC_covered=(\d+) 9P_calls=(\d+)"
                      r" elapsed=([\d.]+)s registry_objects=(\d+)"
                      r" failed_assertions=(\d+)", text)
    if not match:
        raise RuntimeError("missing real campaign evidence line")
    names = ("seed", "random_cases", "verbs", "covered", "p9_calls",
             "elapsed_seconds", "registry_nodes", "failures")
    return dict(zip(names, match.groups()))


def number(row, key):
    return float(row[key])


def render(csv_file, log_file):
    verbs = syscall_verbs()
    data = read_metrics(csv_file)
    run = read_run(log_file)
    expected = {f"{path}/{verb}" for verb in verbs for path in PATHS}
    if set(data) != expected | {"9P_SERVER"}:
        raise RuntimeError(f"missing or extra dispatch rows: {sorted(set(data) ^ (expected | {'9P_SERVER'}))}")
    total = sum(int(data[name]["calls"]) for name in expected)
    # The actual C harness preflights each of the 21 verbs under all
    # 3 paths ONCE, then dispatches random_cases. These are REAL calls
    # and must be counted, not silently stripped to fit the label.
    preflight = len(expected)
    if total != int(run["random_cases"]) + preflight or \
       int(run["covered"]) != len(expected):
        raise RuntimeError(
            f"real calls mismatch: recorded={total}, random={run['random_cases']}, "
            f"preflight_expected={preflight}")
    if int(data["9P_SERVER"]["calls"]) != int(run["p9_calls"]) or \
       int(run["failures"]) != 0:
        raise RuntimeError("9P count or assertion failure mismatch")
    if int(run["verbs"]) != len(verbs):
        raise RuntimeError("syscall table grew without requalifying corpus")

    groups = []
    for verb in verbs:
        group = {path: data[f"{path}/{verb}"] for path in PATHS}
        group["name"] = verb
        groups.append(group)
    groups.sort(key=lambda group: number(group["REG_IPC"], "mean_ns"),
                reverse=True)
    lines = [
        "# P8 — Measured real hosted syscall fuzz profile",
        "",
        "Status: empirical hostname-specific workload, NOT complete P8, NXEC",
        "authority qualification, kernel guest execution or 9P2000.L.",
        "Source: actual kernel/syscall.c dispatch, wire KernelMsg encode/decode",
        "and a real POSIX REG_IPC queue, not stub handlers.",
        "",
        "## Evidence and reproducibility",
        "",
        f"- Seed: {run['seed']}. Actual randomized dispatches: "
        f"{int(run['random_cases']):,} + {preflight} real preflight calls "
        f"= {total:,} across {len(expected)} verb/path pairs.",
        f"- Separate 9P server calls: {int(run['p9_calls']):,}. "
        f"All assertion failures: {run['failures']}.",
        f"- Wall elapsed: {run['elapsed_seconds']} s; remaining NXREG objects: "
        f"{run['registry_nodes']}. Counts agree between log and CSV.",
        f"- CSV SHA256: {hashlib.sha256(csv_file.read_bytes()).hexdigest()}.",
        "- Latencies are per-real-call, including the relevant transport path,",
        "  with host wall-clock noise and profiling overhead.",
        "- p99 values are *upper bounds of logarithmic latency buckets*,",
        "  NOT exact quantiles. Rejections are valid typed failures under",
        "  hostile workloads, NOT successful operations or assertion errors.",
        "",
        "## Full per-syscall telemetry (ranked by REG_IPC mean)",
        "",
        "| Verb | Direct mean us | Wire mean us | REG IPC mean us | REG IPC calls | REG IPC p99 upper us | REG IPC Buddy requests/call |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for group in groups:
        direct, wire, ipc = (group[name] for name in PATHS)
        lines.append(
            "| {name} | {direct:.2f} | {wire:.2f} | {ipc:.2f} | "
            "{calls:,} | {p99:.2f} | {alloc:.2f} |".format(
                name=group["name"],
                direct=number(direct, "mean_ns") / 1000.0,
                wire=number(wire, "mean_ns") / 1000.0,
                ipc=number(ipc, "mean_ns") / 1000.0,
                calls=int(ipc["calls"]),
                p99=number(ipc, "p99_upper_ns") / 1000.0,
                alloc=int(ipc["buddy_requests"]) / int(ipc["calls"])))
    # All latency and allocator results are taken from the REAL
    # per-verb counters; never fabricate or interpolate missing rows.
    lines += [
        "",
        "## All 63 measured syscall/path combinations",
        "",
        "| Path/verb | Calls | Rejected | Mean us | P50 upper us | "
        "P95 upper us | P99 upper us | Max us | Buddy success | "
        "Buddy frees | Buddy failures | Page success | Page frees |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for path in PATHS:
        for verb in verbs:
            row = data[f"{path}/{verb}"]
            values = [
                f"{path}/{verb}",
                f"{int(row['calls']):,}",
                f"{int(row['rejected']):,}",
                f"{number(row, 'mean_ns') / 1000.0:.3f}",
                f"{number(row, 'p50_upper_ns') / 1000.0:.3f}",
                f"{number(row, 'p95_upper_ns') / 1000.0:.3f}",
                f"{number(row, 'p99_upper_ns') / 1000.0:.3f}",
                f"{number(row, 'max_ns') / 1000.0:.3f}",
                f"{int(row['buddy_success']):,}",
                f"{int(row['buddy_frees']):,}",
                f"{int(row['buddy_failures']):,}",
                f"{int(row['page_success']):,}",
                f"{int(row['page_frees']):,}",
            ]
            lines.append("| " + " | ".join(values) + " |")
    total_buddy_success = sum(int(data[key]["buddy_success"]) for key in expected)
    total_buddy_frees = sum(int(data[key]["buddy_frees"]) for key in expected)
    total_buddy_failures = sum(int(data[key]["buddy_failures"]) for key in expected)
    total_pages = sum(int(data[key]["page_success"]) for key in expected)
    total_page_frees = sum(int(data[key]["page_frees"]) for key in expected)
    lines += [
        "",
        f"- Counted Buddy successes: {total_buddy_success:,}; "
        f"counted frees: {total_buddy_frees:,}; "
        f"allocation failures: {total_buddy_failures:,}.",
        f"- Counted page successes: {total_pages:,}; "
        f"page frees: {total_page_frees:,}.",
        "- These deltas measure the instrumented syscall windows ONLY.",
        "  They are NOT a full-process leak proof: startup, retained objects,",
        "  exit reclamation and provider-internal caches are outside windows.",
        "",
    ]
    p9 = data["9P_SERVER"]
    lines += [
        "",
        "## Interpretation and missing gates",
        "",
        f"- Separate 9P_SERVER frame test mean: "
        f"{number(p9, 'mean_ns') / 1000.0:.3f} us across "
        f"{int(p9['calls']):,} requests. This is NOT a complete VFS read/write.",
        "- The slowest hosted verbs are stat/fstat/open/create: the current",
        "  kernel/vfs.c pathname-to-inode lookup performs repeated linear scans",
        "  and registry Value retrieval. This source observation is a",
        "  *candidate bottleneck*, not an instruction-level causal profile.",
        "- REG_IPC adds serialization/copy/queue overhead; comparing means",
        "  from differently randomized calls cannot alone identify which",
        "  component accounts for every nanosecond.",
        "- No process-wide, per-domain authority cache invalidation,",
        "  shared-page lease transport or full VFS transaction is proven.",
        "- Page counters and Buddy request/byte/failure metrics per path",
        "  remain available in the source CSV (build/assurance) and are",
        "  counted from production allocators. A failed request is not a",
        "  successfully allocated page.",
        "- Next P3/P5 acceptance requires canonical indexed NXOLMO named",
        "  relationships replacing the linear RegKey/VFS pointer trees,",
        "  then repeated controlled 1/2/4/8/16/32-worker measurements",
        "  and cache/NUMA/page-fault instrumentation.",
        "",
        "The exact input CSV/log are held in build/assurance and are excluded",
        "from Git as runtime outputs; this report includes their provenance",
        "and never fabricates missing metrics.",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=CSV_DEFAULT)
    parser.add_argument("--log", type=Path, default=LOG_DEFAULT)
    parser.add_argument("--out", type=Path, default=OUTPUT_DEFAULT)
    args = parser.parse_args()
    try:
        report = render(args.csv, args.log)
    except (KeyError, ValueError, RuntimeError, OSError) as exc:
        print(f"SYS_METRIC_REPORT_FAIL: {exc}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(report)
    print(f"SYS_METRIC_REPORT_PASS {args.out} records=63 kernel_verbs=21")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
