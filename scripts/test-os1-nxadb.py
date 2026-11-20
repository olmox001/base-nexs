#!/usr/bin/env python3
"""End-to-end NexsOS1 nxscript verification in a *visible* QEMU guest.

All source edits are scoped to base-nexs. The existing NexsOS1 build process
produces the OS images, then this test installs the new nxscript ELF into the
generated rootfs and recreates its disk. No NX-SDK sources are altered.

The QEMU guest is started through the existing nxadb lifecycle, and results
are verified from real guest UART/output and QMP screenshots. The optional
--keep-running leaves the guest GUI open for manual inspection.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
ROOT = BASE.parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nxadb  # noqa: E402


def checked(command: list[str], log: Path | None = None) -> None:
    if log is None:
        subprocess.run(command, cwd=ROOT, check=True)
        return
    with log.open("w", encoding="utf-8") as output:
        proc = subprocess.run(command, cwd=ROOT, stdout=output,
                              stderr=subprocess.STDOUT, check=False)
    if proc.returncode:
        tail = log.read_text(errors="replace")[-3500:]
        raise RuntimeError(
            f"{command[0]} exited {proc.returncode}: {log}\n{tail}")


def prepare(arch: str) -> tuple[Path, Path]:
    arch_dir = "amd64" if arch == "amd64" else "arm64"
    binary = BASE / "build" / f"os1-{arch_dir}" / "nxscript.elf"
    if not binary.is_file():
        raise FileNotFoundError(f"OS1 ELF absent: {binary}")
    out = BASE / "build" / "nxadb-os1" / arch
    out.mkdir(parents=True, exist_ok=True)

    # The canonical build validates kernel, SDK, rootfs and architecture.
    checked(["make", f"ARCH={arch}", "direct-artifacts"],
            out / "system-build.log")

    rootfs = ROOT / "build" / arch / "rootfs"
    if not (rootfs / "bin").is_dir():
        raise FileNotFoundError(f"Guest image missing: {rootfs}")

    guest = rootfs / "bin" / "nxscript"
    shutil.copy2(binary, guest)
    guest.chmod(0o755)

    # AOT is produced by the public --compile --target os1-* contract.
    # Test the real generated ELF, not just a linked file header.
    aot = BASE / "build" / "assurance" / f"cli-os1-{arch_dir}.elf"
    if not aot.is_file():
        raise FileNotFoundError(f"Missing OS1 AOT acceptance ELF: {aot}")
    guest_aot = rootfs / "bin" / "nxscript-aot"
    shutil.copy2(aot, guest_aot)
    guest_aot.chmod(0o755)

    smoke = rootfs / "home" / "shared" / "nxscript-smoke.nx"
    smoke.write_text(
        'out 21 * 2\n'
        'out "NXScript-OS1-NATIVE-READY"\n',
        encoding="ascii")

    # mkdisk preserves the existing GPT/ext4 layout and runtime image policy.
    disk = ROOT / "build" / arch / "disk.img"
    checked([str(ROOT / "tools" / "mkdisk"), str(disk),
             str(rootfs), "--extents"],
            out / "mkdisk.log")
    return out, smoke


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("amd64", "aarch64"),
                        required=True)
    parser.add_argument("--display", choices=("default", "none"),
                        default="default")
    parser.add_argument("--keep-running", action="store_true")
    args = parser.parse_args()

    session = nxadb.Session.create(args.arch, ROOT, "direct", Path("build"))
    if session.is_running():
        raise RuntimeError(
            "The existing nxadb session is running; refusing to replace it")
    # Never rebuild or rewrite an image while the guest owns the disk.
    out, smoke = prepare(args.arch)

    session.display = args.display
    records: list[dict[str, object]] = []
    success = False
    started = False
    try:
        nxadb.start_qemu(session, wait=True)
        started = True

        # A visible prompt may predate the asynchronous boot-time NXTEST TTY.
        # NEVER inject user commands before the guest finishes its startup suite.
        boot_ok, boot_state = nxadb._wait_for_boot_nxtest(session)
        records.append({
            "name": "system-boot-suite", "passed": boot_ok,
            "state": boot_state, "scope": "existing NexsOS1 integration",
        })
        print(f"OS1_{args.arch}_SYSTEM_BOOT_SUITE={boot_state}", flush=True)
        if boot_state in ("timeout", "fault", "running"):
            raise RuntimeError("Guest startup tests did not finish")
        if not nxadb.wait_for_shell(session, timeout=50):
            raise RuntimeError("No stable NXShell after startup suite")
        for key, command, required in (
            ("version", "/bin/nxscript --version", "Buddy/Registry Runtime"),
            ("smoke", "/bin/nxscript /home/shared/nxscript-smoke.nx",
             "NXScript-OS1-NATIVE-READY"),
            ("aot", "/bin/nxscript-aot", "NXSCRIPT_BAREMETAL_SMOKE"),
        ):
            code, raw = nxadb.run_shell_c(session, command, timeout=55)
            cleaned = nxadb.strip_ansi(raw)
            passed = code == 0 and required in cleaned
            records.append({
                "name": key, "command": command,
                "nxadb_status": code, "passed": passed,
                "output_tail": cleaned[-2500:],
            })
            print(f"OS1_{args.arch}_{key.upper()}="
                  f"{'PASS' if passed else 'FAIL'}", flush=True)
            if not passed:
                break
        shot = nxadb.cmd_screenshot(session)
        records.append({"name": "screenshot", "passed": shot == 0})
        native_ok = (
            len(records) == 5 and
            all(bool(x["passed"]) for x in records[1:])
        )
        # Full system acceptance must also acknowledge the independent boot
        # regression suite; do not call a faulty host image "all tests pass".
        success = native_ok and boot_ok
        print(f"OS1_{args.arch}_NXSCRIPT_PROBE="
              f"{'PASS' if native_ok else 'FAIL'}", flush=True)
    except Exception as exc:
        records.append({"name": "exception", "passed": False,
                        "error": repr(exc)})
        print(f"OS1_{args.arch}_EXCEPTION={exc}", file=sys.stderr)
    finally:
        report = out / "nxadb-verification.json"
        report.write_text(json.dumps({
            "arch": args.arch, "display": args.display,
            "verified": success,
            "records": records,
            "uart": str(session.ser_log),
            "qmp": str(session.qmp_sock),
        }, indent=2) + "\n", encoding="utf-8")
        if started and not args.keep_running:
            nxadb.stop_qemu(session)
        print(f"OS1_{args.arch}_RESULT="
              f"{'PASS' if success else 'FAIL'}", flush=True)
        print(f"REPORT={report}", flush=True)
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
