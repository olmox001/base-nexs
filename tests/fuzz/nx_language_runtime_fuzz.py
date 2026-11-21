#!/usr/bin/env python3
"""Antagonistic real NEXS language/parser/runtime execution, not a mock.
Product Power12 applies to production, not hostile test-generation code.
Valid cases use a Python-computed oracle; malformed cases check containment.
Every case executes the actual nxscript executable in an isolated subprocess.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARTIFACT = ROOT / "build/assurance/nx_language_fuzz"
BEGIN = "__NX_FUZZ_RESULT_BEGIN__"
END = "__NX_FUZZ_RESULT_END__"
VARIANTS = ("arithmetic", "function", "registry", "array", "conditional",
            "loop", "strings", "filesystem", "syntax_adversarial",
            "runtime_adversarial")


def valid_program(rng: random.Random, variant: str, case_id: int, directory: Path):
    a, b = rng.randint(-1000, 1000), rng.randint(-1000, 1000)
    prefix = [f'out "{BEGIN}"']
    suffix = [f'out "{END}"']
    if variant == "arithmetic":
        op = rng.choice(("+", "-", "*"))
        result = {"+": a + b, "-": a - b, "*": a * b}[op]
        source = [f"a={a}", f"b={b}", f"out a {op} b"]
        oracle = [str(result)]
    elif variant == "function":
        offset = rng.randint(1, 400)
        source = [f"fn shift(v) {{ ret v+{offset} }}", f"out shift({a})"]
        oracle = [str(a + offset)]
    elif variant == "registry":
        value = rng.randint(-100000, 100000)
        path = f"/adversarial/language/case_{case_id}/leaf"
        source = [f'reg_set("{path}", {value})',
                  f'out reg_get("{path}")',
                  f'reg_del("/adversarial/language/case_{case_id}")',
                  f'out type(reg_get("{path}"))']
        oracle = [str(value), "err"]
    elif variant == "array":
        first, second = rng.randint(0, 30000), rng.randint(0, 30000)
        source = [f"ary[0]={first}", f"ary[1]={second}",
                  "ary[2]=ary[0]+ary[1]", "out ary[2]"]
        oracle = [str(first + second)]
    elif variant == "conditional":
        left, right = rng.randint(0, 300), rng.randint(0, 300)
        source = [f"x={left}", f"y={right}",
                  'if x > y { out "G" } else { out "L" }']
        oracle = ["G" if left > right else "L"]
    elif variant == "loop":
        n = rng.randint(1, 120)
        source = [f"limit={n}", "i=0", "acc=0",
                  "loop { if i >= limit { break } acc=acc+i i=i+1 }",
                  "out acc"]
        oracle = [str(n * (n - 1) // 2)]
    elif variant == "strings":
        size = rng.randrange(0, 2500)
        raw = "".join(rng.choice("abcdefgh0123456789") for _ in range(size))
        source = [f'v="{raw}"', "out len(v)"]
        oracle = [str(size)]
    elif variant == "filesystem":
        text = "".join(rng.choice("abcdefgh0123456789") for _ in range(24))
        path = directory / f"case_{case_id}.data"
        # Path is generator-owned, no user-controlled shell expansion.
        source = [f'fd=create("{path}",1)', f'write(fd,"{text}")',
                  "close(fd)", f'fd2=open("{path}",0)',
                  "out read(fd2,100)", "close(fd2)",
                  f'remove("{path}")']
        oracle = [text]
    else:
        raise ValueError(variant)
    return "\n".join(prefix + source + suffix) + "\n", oracle


def mutated_program(rng: random.Random, variant: str):
    tokens = [
        b"fn f(x) { ret x+1 \n",
        b"out \"unterminated\n",
        b"fn f(\xff) { out 1 }\n",
        b"out 99999999999999999999999999999999999\n",
        b"out \"\\x00bad\" \n",
        b"reg_set(\"/x//y\",99)\nout type(reg_get(\"/x/y\"))\n",
        b"out (" * 80 + b"1\n",
        b"} else { break } else {\n",
        b"fn w(x) { ret x }\nout w()\n",
        b"\x00fn f(x){ret x}\n",
        b"out \"\\\\" * 400 + b"\n",
        b"fn p(x) { ret p(x) }\nout p(42)\n",
    ]
    data = bytearray(rng.choice(tokens))
    if variant == "runtime_adversarial":
        # A malformed but syntactically legal-looking expression may
        # execute; return-code success is NOT evidence that it was correct.
        data = bytearray(rng.choice([
            b'out 1 / 0\n',
            b'out reg_get("/does/not/exist")\n',
            b'out arr[99999999]\n',
            b'out (9223372036854775807+1)\n',
            b'fn f(x){ret x+1}\nout f("bad")\n',
            b'out len(1)\n'
        ]))
    if rng.random() < 0.35:
        for _ in range(rng.randrange(1, 7)):
            op = rng.randrange(5)
            if op == 0 and data:
                data[rng.randrange(len(data))] = rng.randrange(0, 256)
            elif op == 1:
                data.extend(bytes((rng.randrange(256),)))
            elif op == 2 and data:
                del data[rng.randrange(len(data))]
            elif op == 3 and len(data) < 8192:
                data.extend(b"{")
            elif op == 4 and len(data) < 8192:
                data.extend(b")")
    return bytes(data)


def execute(binary: Path, program: Path, timeout: float, syntax: bool):
    argv = [str(binary), "--check-syntax", str(program)] if syntax else \
           [str(binary), str(program)]
    start = time.monotonic_ns()
    try:
        result = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=timeout,
                                check=False)
        return result.returncode, result.stdout, result.stderr, \
               time.monotonic_ns() - start, None
    except subprocess.TimeoutExpired as exc:
        return -999, exc.stdout or b"", exc.stderr or b"", \
               time.monotonic_ns() - start, "timeout"
    except OSError as exc:
        return -998, b"", str(exc).encode("utf-8", "replace"), \
               time.monotonic_ns() - start, "host-exec"


def extract_output(content: bytes):
    text = content.decode("utf-8", "replace")
    lines = text.splitlines()
    try:
        begin = lines.index(BEGIN)
        end = lines.index(END, begin + 1)
        return lines[begin + 1:end]
    except ValueError:
        return None


def percent(values, percentile: float):
    if not values:
        return 0
    ordered = sorted(values)
    return ordered[math.ceil((len(ordered) * percentile)) - 1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", type=int, default=256)
    parser.add_argument("--seed", type=lambda v: int(v, 0), default=0x20261121)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--binary", type=Path, default=ROOT / "nxscript")
    parser.add_argument("--output", type=Path, default=DEFAULT_ARTIFACT)
    args = parser.parse_args()
    if args.cases < 1 or args.cases > 1000000 or args.timeout <= 0:
        parser.error("invalid bounded campaign size/timeout")
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error("real nxscript binary missing")
    args.output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    stats = {}
    rows, failures = [], []
    cases = list(VARIANTS)
    cases.extend(rng.choices(VARIANTS, k=max(0, args.cases - len(cases))))
    cases = cases[:args.cases]
    with tempfile.TemporaryDirectory(prefix="nx-fuzz-", dir=args.output) as temp:
        directory = Path(temp)
        for n, variant in enumerate(cases):
            valid = variant not in ("syntax_adversarial", "runtime_adversarial")
            if valid:
                source, expected = valid_program(rng, variant, n, directory)
                source_bytes = source.encode("utf-8")
            else:
                expected = None
                source_bytes = mutated_program(rng, variant)
            source_path = directory / f"case_{n}.nx"
            source_path.write_bytes(source_bytes)
            digest = hashlib.sha256(source_bytes).hexdigest()
            # All valid cases must parse. Malformed cases may be rejected
            # syntactically or by the running evaluator (but must not crash).
            scode, sout, serr, stime, serrkind = execute(
                binary, source_path, args.timeout, syntax=True)
            code, out, err, ns, errkind = execute(
                binary, source_path, args.timeout, syntax=False)
            actual = extract_output(out) if valid else None
            failure = None
            if serrkind or errkind:
                failure = serrkind or errkind
            elif scode < 0 or code < 0:
                failure = f"signal syntax={scode} runtime={code}"
            elif valid and (scode != 0 or code != 0):
                failure = f"valid case rejected syntax={scode} runtime={code}"
            elif valid and actual != expected:
                failure = f"oracle_mismatch expected={expected!r} actual={actual!r}"
            if failure:
                saved = args.output / f"failure_{n}_{digest[:12]}.nx"
                saved.write_bytes(source_bytes)
                failures.append({"case": n, "kind": variant, "failure": failure,
                                 "source": str(saved), "seed": args.seed,
                                 "sha256": digest, "stderr":
                                 err[-1500:].decode("utf-8", "replace")})
                print("NX_FUZZ_FAIL", json.dumps(failures[-1]), file=sys.stderr)
            stats.setdefault(variant, []).append(ns)
            rows.append({"case": n, "variant": variant,
                         "valid_oracle": int(valid), "sha256": digest,
                         "syntax_rc": scode, "runtime_rc": code,
                         "syntax_ns": stime, "runtime_ns": ns,
                         "failure": failure or ""})
    with (args.output / "cases.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    profile = {}
    for variant, values in stats.items():
        profile[variant] = {"calls": len(values),
                            "total_ns": sum(values),
                            "mean_ns": int(statistics.mean(values)),
                            "p50_ns": percent(values, .50),
                            "p95_ns": percent(values, .95),
                            "p99_ns": percent(values, .99),
                            "max_ns": max(values)}
    summary = {"seed": args.seed, "cases": len(rows), "binary": str(binary),
               "failures": failures, "per_variant": profile,
               "coverage_categories": sorted(stats.keys()),
               "valid_oracle_cases": sum(r["valid_oracle"] for r in rows),
               "malformed_containment_cases": sum(1 - r["valid_oracle"]
                                                 for r in rows),
               "coverage_limit": "Only generated grammar families and host interpreter; "
                                 "not all syntax, branches or guest syscalls"}
    (args.output / "report.json").write_text(json.dumps(summary, indent=2))
    print(f"NEXS LANGUAGE FUZZ seed={args.seed} cases={len(rows)} "
          f"oracle={summary['valid_oracle_cases']} "
          f"hostile={summary['malformed_containment_cases']} "
          f"failures={len(failures)} report={args.output / 'report.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
