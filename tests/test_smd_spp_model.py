"""SMD/SPP mathematical research invariants; not production allocator proof."""
from __future__ import annotations
import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/research/smd_spp_v01.py"
spec = importlib.util.spec_from_file_location("smd_research", SCRIPT)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


class SMDResearchTests(unittest.TestCase):
    def test_all_policies_admit_only_nonoverlapping_valid_extents(self):
        for seed in range(10):
            trace = module.common_trace(seed, 140)
            for policy in ("first-fit", "best-fit", "worst-fit", "spp"):
                layout = module.Layout(policy)
                for operation, cid, first, last in trace:
                    if operation == "create":
                        layout.create(cid, first, last)
                    elif operation == "resize":
                        layout.resize(cid, first, last)
                    else:
                        layout.delete(cid, first)
                    layout.verify()
                report = layout.report()
                self.assertEqual(
                    500, report["logical_mib"] +
                    report["reserved_mib"] + report["free_mib"])
                self.assertGreaterEqual(report["free_mib"], 0)

    def test_rejected_create_leaves_committed_state_unchanged(self):
        for policy in ("first-fit", "best-fit", "worst-fit", "spp"):
            layout = module.Layout(policy, capacity=30)
            self.assertTrue(layout.create(1, {0: 8}))
            old = layout.report()
            self.assertFalse(layout.create(2, {0: 31}))
            self.assertEqual(old["logical_mib"], layout.report()["logical_mib"])
            self.assertEqual(old["reserved_mib"], layout.report()["reserved_mib"])
            self.assertEqual([1], list(layout.live))

    def test_failed_pinned_relocation_preserves_original(self):
        layout = module.Layout("first-fit", capacity=40)
        self.assertTrue(layout.create(1, {1: 9}))
        self.assertTrue(layout.create(2, {1: 12}))
        layout.live[1].pinned = True
        self.assertFalse(layout.resize(1, 1, 20))
        self.assertEqual(9, layout.live[1].size)
        self.assertEqual(0, layout.live[1].start)
        layout.verify()

    def test_zero_and_incorrect_forecast_cannot_bypass_capacity(self):
        layout = module.Layout("spp", capacity=17)
        self.assertTrue(layout.create(1, {1: 12}, forecast=9999))
        self.assertFalse(layout.create(2, {1: 8}, forecast=0))
        self.assertEqual(12, layout.live[1].size)
        layout.verify()

    def test_invalid_resource_states_do_not_pass(self):
        layout = module.Layout("spp", capacity=20)
        self.assertFalse(layout.create(1, {1: -1}))
        self.assertFalse(layout.create(1, {1: 0}))
        self.assertFalse(layout.create(1, {}))
        self.assertTrue(layout.create(1, {1: 5}))
        self.assertFalse(layout.resize(1, 1, -20))
        self.assertFalse(layout.resize(1, 55, 3))
        self.assertEqual(5, layout.live[1].size)

    def test_deletion_releases_capacity_and_preserves_other_containers(self):
        layout = module.Layout("first-fit", capacity=20)
        self.assertTrue(layout.create(1, {1: 8}))
        self.assertTrue(layout.create(2, {1: 12}))
        self.assertTrue(layout.delete(1, 1))
        self.assertEqual(12, layout.live[2].size)
        self.assertEqual(8, layout.report()["free_mib"])
        self.assertTrue(layout.create(3, {1: 8}))
        layout.verify()

    def test_seeded_trace_and_results_reproducible(self):
        self.assertEqual(module.common_trace(4, 45), module.common_trace(4, 45))
        self.assertEqual(module.experiment(4, 45), module.experiment(4, 45))
        self.assertNotEqual(module.common_trace(4), module.common_trace(5))

    def test_policy_is_not_falsely_reported_as_dominant(self):
        traces = [module.experiment(seed, 120) for seed in range(4)]
        self.assertTrue(all(len(rows) == 4 for rows in traces))
        for rows in traces:
            metrics = {entry["policy"]: entry for entry in rows}
            self.assertGreaterEqual(metrics["spp"]["rejected"], 0)
            self.assertIn("moved_mib", metrics["spp"])
            self.assertGreaterEqual(metrics["spp"]["moved_mib"], 0)
        self.assertNotIn("PASS_TARGET", SCRIPT.read_text())

    def test_cli_fails_closed_on_unbounded_experiment(self):
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), "--seeds", "0"],
            capture_output=True, text=True, timeout=5)
        self.assertNotEqual(0, proc.returncode)
        self.assertIn("invalid finite experiment budget", proc.stderr)

    def test_reports_are_explicitly_simulation_only(self):
        proc = subprocess.run(
            [sys.executable, str(SCRIPT), "--seeds", "2", "--steps", "20"],
            capture_output=True, text=True, timeout=10)
        self.assertEqual(0, proc.returncode, proc.stderr)
        report = json.loads(proc.stdout)
        self.assertIn("SIMULATION_ONLY", report["status"])
        self.assertEqual(8, len(report["reports"]))


if __name__ == "__main__":
    unittest.main()
