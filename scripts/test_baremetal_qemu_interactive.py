#!/usr/bin/env python3
"""Real UART QEMU interactive NEXS REPL qualification, no host-side fake prompt."""
from __future__ import annotations

import argparse
import json
import os
import re
import select
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Every probe must produce an independently verifiable output line.
# Multi-line probes cover writes and function creation, but a redraw of
# the prompt on each keystroke NEVER counts as command completion.
PROBES = (
    ("out 2+3", "5"),
    ('reg_set("/integration/uart", 731)\nout reg_get("/integration/uart")',
     "731"),
    ("fn triple(v) { ret v*3 }\nout triple(7)", "21"),
    ('out len("real uart")', "9"),
)
# Additional live guest probes: bundled source is a REAL registry object,
# not a shadow /vfs/files or /sys/vfs/files identity. Verify cd+ls output.
NAMESPACE_PROBES = (
    ('out type(reg_get("/nxlib/stdlib.nx"))', "str"),
    ('out type(reg_get("/nxlib/services/fs/init.nx"))', "str"),
    ('out type(reg_get("/nxlib/fs.nx"))', "str"),
    ('out type(reg_get("/sys/loader/libs/stdlib"))', "int"),
    ('out type(reg_get("/sys/loader/libs/fs"))', "int"),
    ('out type(reg_get("/sys/auth/status"))', "err"),
    ('out type(reg_get("/sys/vfs/files/nxlib/stdlib.nx"))', "err"),
    ('out type(reg_get("/vfs/files/nxlib/stdlib.nx"))', "err"),
    ("cd sys\nls\nout 31", "31", ("[REGISTRY] /sys",)),
    ("cd /nxlib\nls\nout 41", "41", ("stdlib.nx",)),
    ('fn nested_identity(x) { ret x }\n'
     'fn nested_forward(x) { ret nested_identity(x) }\n'
     'out nested_forward(77)', "77"),
    ("cd /\nout basename(\"/one/two\")", "two"),
    ("out vfs_exists(\"/nxlib/stdlib.nx\")", "1"),
)
PROMPT = "nexs> "


def plain_text(blob: bytes) -> str:
    text = blob.decode("utf-8", "replace")
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)


def output_value_present(segment: str, value: str) -> bool:
    if value is None:
        return True
    normalized = segment.replace("\r", "\n")
    return re.search(r"(?m)^[ \t]*" + re.escape(value) + r"[ \t]*$", normalized) is not None


def run_qualification(arch: str, binary: Path, timeout: float,
                      probes=PROBES):
    common = ["-m", "512M", "-kernel", str(binary),
              "-display", "none", "-monitor", "none",
              "-serial", "stdio", "-no-reboot"]
    argv = (["qemu-system-x86_64", "-machine", "pc,smm=off"] if arch == "amd64"
            else ["qemu-system-aarch64", "-machine", "virt",
                  "-cpu", "cortex-a57"]) + common
    child = subprocess.Popen(
        argv, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, start_new_session=True, bufsize=0
    )
    transcript = bytearray()
    stage = -1
    segment_start = 0
    sent_at = 0
    timings = []
    error = None
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            if child.poll() is not None:
                error = f"qemu-early-exit: {child.returncode}"
                break
            readable, _, _ = select.select(
                [child.stdout], [], [], min(.25, max(0, deadline - time.monotonic())))
            if readable:
                block = os.read(child.stdout.fileno(), 8192)
                if not block:
                    error = "guest-stdout-closed"
                    break
                transcript.extend(block)
                if len(transcript) > 2 * 1024 * 1024:
                    error = "oversized-or-runaway-uart"
                    break
            text = plain_text(bytes(transcript))
            faults = ("*** NEXS EXCEPTION ***", "*** HALTED ***",
                      "[NEXS FATAL ERROR]", "Kernel panic")
            if any(marker in text for marker in faults):
                error = "guest-fault-marker"
                break
            if stage == -1:
                pos = text.find(PROMPT)
                if pos < 0 or "Special commands:" not in text:
                    continue
                stage = 0
                segment_start = pos + len(PROMPT)
                sent_at = time.monotonic_ns()
                child.stdin.write((probes[0][0] + "\n").encode())
                child.stdin.flush()
                continue
            item = probes[stage]
            command, expected = item[:2]
            required = item[2] if len(item) > 2 else ()
            # The line editor redraws its prompt for every keystroke.
            # Find an EXACT result line first, then a subsequent prompt.
            # A console redraw before the result is not a reply.
            segment = text[segment_start:]
            normalized = segment.replace("\r", "\n")
            found = re.search(
                r"(?m)^[ \\t]*" + re.escape(expected) + r"[ \\t]*$",
                normalized
            )
            if found is None:
                continue
            # Compare in the original string: CR to LF normalization
            # preserves length and therefore the match offsets.
            result_end = segment_start + found.end()
            pos = text.find(PROMPT, result_end)
            if pos < 0:
                continue
            segment = text[segment_start:pos]
            if not output_value_present(segment, expected):
                continue
            if any(marker not in segment for marker in required):
                continue
            if "Path '/sys/' not found" in segment or                "Path '/nxlib/' not found" in segment:
                continue
            timings.append({"command": command, "expected": expected,
                            "latency_ns": time.monotonic_ns() - sent_at,
                            "observed": segment[-400:]})
            stage += 1
            if stage == len(probes):
                break
            segment_start = pos + len(PROMPT)
            sent_at = time.monotonic_ns()
            child.stdin.write((probes[stage][0] + "\n").encode())
            child.stdin.flush()
        if stage != len(probes) and error is None:
            error = f"timeout-after-{stage}-replies"
    finally:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            # On restricted macOS test hosts, group signals can be refused
            # despite child ownership; terminate the actual child instead.
            try:
                child.terminate()
            except (ProcessLookupError, PermissionError):
                pass
        try:
            child.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.communicate()
    return {"arch": arch, "passed": stage == len(probes) and error is None,
            "error": error, "completed_probes": len(timings),
            "expected_probes": len(probes), "timings": timings,
            "transcript_tail": plain_text(bytes(transcript))[-4000:],
            "argv": argv}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("amd64", "arm64"), required=True)
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--namespace", action="store_true",
                        help="assert embedded nxlib and direct registry ls/cd")
    args = parser.parse_args()
    binary = ROOT / f"build/baremetal-{args.arch}/nxscript.elf"
    if not binary.is_file() or args.timeout <= 0 or args.timeout > 120:
        parser.error("missing qualified ELF or invalid timeout")
    report = run_qualification(args.arch, binary.resolve(), args.timeout,
                               NAMESPACE_PROBES if args.namespace else PROBES)
    output = ROOT / "build/assurance" / f"baremetal-{args.arch}-{'namespace' if args.namespace else 'interactive'}.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"BAREMETAL_{args.arch}_INTERACTIVE="
          f"{'PASS' if report['passed'] else 'FAIL'} "
          f"completed={report['completed_probes']}/{report['expected_probes']} "
          f"error={report['error']}")
    print("TRANSCRIPT_TAIL=", repr(report["transcript_tail"][-600:]))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
