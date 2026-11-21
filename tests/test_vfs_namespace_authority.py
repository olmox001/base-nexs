# Real hosted runtime VFS namespace denial until authenticated Plan 9 provider.
# Negative authorization tests are not positive Plan 9 mount/link qualification.
from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NEXS = ROOT / "nxscript"


def execute(source: str) -> subprocess.CompletedProcess:
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
        path = Path(temp) / "namespace.nx"
        path.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(NEXS), str(path)], cwd=ROOT,
            capture_output=True, text=True, timeout=15, check=False)


class VfsNamespaceAuthorityTests(unittest.TestCase):
    def test_mount_bind_unmount_reject_without_copying_source_nodes(self):
        result = execute('''
reg_set("/source/one", "ORIGINAL")
reg_set("/destination/keep", "KEEP")
out type(mount("/source", "/destination", 1))
out type(bind("/source", "/destination", 0))
out type(unmount("/source", "/destination"))
out reg_get("/source/one")
out reg_get("/destination/keep")
out type(reg_get("/destination/one"))
out type(reg_get("/sys/fs/mount_count"))
out type(reg_get("/vfs/mount"))
''')
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["err", "err", "err", "ORIGINAL", "KEEP",
                          "err", "err", "err"], result.stdout.splitlines())

    def test_p9_service_does_not_fabricate_sd_registers_or_device_nodes(self):
        result = execute('''
exec("nxlib/services/fs/p9_mnt.nx")
out p9_mnt_init()
out type(reg_get("/sd00"))
out type(reg_get("/sd01"))
out type(reg_get("/sd02"))
out type(reg_get("/sd99"))
out type(reg_get("/sd00/driver/tty0"))
out type(reg_get("/sd01/sys/registry/status"))
''')
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["-1", "err", "err", "err", "err", "err", "err"],
                         result.stdout.splitlines())
        self.assertNotIn("Plan 9 Virtual Kernel Registry", result.stdout)

    def test_filesystem_service_never_advertises_ready_mount(self):
        result = execute('''
exec("nxlib/services/fs/init.nx")
out fs_start()
out fs_mount("/no-source", "/no-target", "9p")
out fs_umount("/no-target")
out type(reg_get("/sys/fs/status"))
out type(reg_get("/sys/fs/mounts"))
''')
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["-1", "-1", "-1", "err", "err"],
                         result.stdout.splitlines())


if __name__ == "__main__":
    unittest.main()
