#!/usr/bin/env python3
"""Execute real Base-NEXS freestanding strncpy on both QEMU ISAs.

The test source is linked into the normal product linker/actual kernel libc
stub. The guest must print its measured result over its own UART. This runner
never infers a PASS from successful compilation or a QEMU exit status.
"""
from __future__ import annotations

import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "assurance"
PASS = b"NEXS_STRNCPY_REAL_GUEST_PASS"
FAIL = b"NEXS_STRNCPY_REAL_GUEST_FAIL"


def run_one(isa: str) -> bool:
    guest = BUILD / f"strncpy-guest-{isa}.elf"
    program = "baremetal-arm64" if isa == "arm64" else "baremetal-amd64"
    build = subprocess.run(
        [
            "make", "-s", program,
            "BAREMETAL_EMBED_C=tests/fixtures/freestanding_strncpy_guest.c",
            "BAREMETAL_TEST_SRCS=kernel/test/test_freestanding_strncpy.c",
            f"BAREMETAL_OUTPUT={guest.relative_to(ROOT)}",
        ],
        cwd=ROOT,
        capture_output=True,
        timeout=100,
        check=False,
    )
    if build.returncode != 0:
        print(f"GUEST_STRNCPY_{isa}=BUILD_FAIL", file=sys.stderr)
        print(build.stderr.decode("utf-8", "replace")[-2000:],
              file=sys.stderr)
        return False

    qemu = (
        ["qemu-system-aarch64", "-machine", "virt", "-cpu", "cortex-a57"]
        if isa == "arm64"
        else ["qemu-system-x86_64", "-machine", "pc,smm=off"]
    )
    qemu += [
        "-m", "512M", "-kernel", str(guest), "-display", "none",
        "-monitor", "none", "-serial", "stdio", "-no-reboot"
    ]
    process = subprocess.Popen(
        qemu, cwd=ROOT, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True, bufsize=0
    )
    output = bytearray()
    try:
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            readable, _, _ = select.select([process.stdout], [], [], 0.1)
            if readable:
                chunk = os.read(process.stdout.fileno(), 4096)
                if chunk:
                    output.extend(chunk)
                else:
                    break
            if PASS in output or FAIL in output:
                break
            if process.poll() is not None and not readable:
                break
    finally:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except (PermissionError, ProcessLookupError):
            try:
                process.terminate()
            except (PermissionError, ProcessLookupError):
                pass
        try:
            process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            try:
                process.kill()
            except (PermissionError, ProcessLookupError):
                pass
            process.communicate(timeout=2)
    BUILD.mkdir(parents=True, exist_ok=True)
    (BUILD / f"strncpy-guest-{isa}.log").write_bytes(output)
    passed = PASS in output and FAIL not in output
    print(f"GUEST_STRNCPY_{isa}={'PASS' if passed else 'FAIL'} "
          f"real_uart_bytes={len(output)}")
    if not passed:
        print(output.decode("utf-8", "replace")[-1200:], file=sys.stderr)
    return passed


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    if not run_one("arm64"):
        return 1
    if not run_one("amd64"):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
