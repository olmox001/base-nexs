#!/usr/bin/env python3
"""Boot Base-NEXS's native baremetal images in isolated QEMU UART sessions."""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("amd64", "arm64"), required=True)
    parser.add_argument("--elf", default=None,
                        help="Optional ELF under build/ (e.g. AOT CLI output)")
    parser.add_argument("--expect", default="NEXS v",
                        help="Required line in guest UART; never accept silent hang")
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args()
    if args.timeout <= 0 or args.timeout > 120:
        parser.error("Timeout must be in the range (0, 120] seconds")

    elf = (BASE / (args.elf or
                   f"build/baremetal-{args.arch}/nxscript.elf")).resolve()
    build_root = (BASE / "build").resolve()
    if not elf.is_relative_to(build_root) or not elf.is_file():
        raise FileNotFoundError(f"Unsafe or missing Base-NEXS ELF: {elf}")
    common = ["-m", "512M", "-kernel", str(elf),
              "-display", "none", "-monitor", "none",
              "-serial", "stdio", "-no-reboot"]
    if args.arch == "amd64":
        argv = ["qemu-system-x86_64", "-machine", "pc,smm=off"] + common
    else:
        argv = ["qemu-system-aarch64", "-machine", "virt",
                "-cpu", "cortex-a57"] + common

    proc = subprocess.Popen(
        argv, cwd=BASE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True)
    timed_out = False
    try:
        data, _ = proc.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            data, _ = proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            data, _ = proc.communicate()
    output = data.decode("utf-8", "replace")
    report = BASE / "build/assurance" / f"baremetal-{args.arch}-qemu.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    # Ignore ANSI styling, not error content; never match the language name
    # in a panic header as evidence of a working interpreter.
    plain = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", output)
    matched = args.expect in plain
    faults = (
        "*** NEXS EXCEPTION ***", "*** HALTED ***",
        "[NEXS FATAL ERROR]", "Kernel panic", "SVE/FP/SIMD",
    )
    fault_found = [marker for marker in faults if marker in plain]
    prompt_ready = ("Special commands:" in plain and "nexs" in plain) if args.expect == "NEXS v" else matched
    pass_test = matched and prompt_ready and not fault_found and (timed_out or proc.returncode == 0)
    report.write_text(json.dumps({
        "arch": args.arch, "passed": pass_test,
        "timed_out": timed_out,
        "exit_code": proc.returncode,
        "expected_marker": args.expect,
        "fault_markers": fault_found,
        "prompt_ready": prompt_ready,
        "guest_output_tail": output[-4000:],
        "qemu_argv": argv,
    }, indent=2) + chr(10))
    print(f"BAREMETAL_{args.arch}_QEMU="
          f"{'PASS' if pass_test else 'FAIL'}")
    print(f"UART_FINAL={output[-500:]!r}")
    print(f"REPORT={report}")
    return 0 if pass_test else 1


if __name__ == "__main__":
    raise SystemExit(main())
