"""Executable invariants for the upgraded LIMEN-SM file-extent experiment.

These are mathematical/resource accounting tests; not actual kernel/REG/VM.
"""
from __future__ import annotations
import importlib.util
import json
import math
import subprocess
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"scripts/research/limen_sm_v02.py"
spec=importlib.util.spec_from_file_location("limen_research_v02",SRC)
limen=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=limen
spec.loader.exec_module(limen)


class LIMENModelTests(unittest.TestCase):
    def test_all_policies_preserve_provider_capacity_each_event(self):
        trace=limen.common_events(17,65)
        for policy in limen.POLICIES:
            world=limen.Domain(policy)
            for kind,cid,a,b in trace:
                if kind=="create":
                    world.create(cid,a,b)
                elif kind=="resize":
                    world.resize(cid,a,b)
                elif kind=="unlink":
                    world.unlink(cid,a)
                else:
                    world.touch(cid,a,b)
                world.verify()
                r=world.report()
                self.assertEqual(500,r["allocated_mib"]+r["reserved_mib"]+
                                     r["free_mib"])
                self.assertGreaterEqual(r["free_mib"],0)

    def test_container_is_multiple_file_extents_not_a_global_span(self):
        world=limen.Domain("first-fit",60)
        self.assertTrue(world.create(1,{1:8}))
        self.assertTrue(world.create(2,{1:6}))
        self.assertTrue(world.create(1+2,{1:3}))
        self.assertTrue(world.unlink(2,1))
        self.assertTrue(world.create(4,{1:5,2:5}))
        self.assertEqual(2,len(world.dirs[4]))
        self.assertEqual(10,sum(world.blobs[oid].allocated
                                for oid in world.dirs[4].values()))
        self.assertGreater(len(world.extents()),len(world.dirs))
        world.verify()

    def test_shared_hardlink_counted_once_and_retains_extent(self):
        world=limen.Domain("best-fit",32)
        self.assertTrue(world.create(1,{0:11}))
        before=world.report()["allocated_mib"]
        self.assertTrue(world.link(1,0,2,7))
        self.assertEqual(before,world.report()["allocated_mib"])
        self.assertEqual(1,world.report()["shared_objects"])
        self.assertTrue(world.unlink(1,0))
        self.assertEqual(before,world.report()["allocated_mib"])
        self.assertTrue(world.unlink(2,7))
        self.assertEqual(0,world.report()["allocated_mib"])
        world.verify()

    def test_copy_on_write_uses_independent_physical_blocks(self):
        world=limen.Domain("first-fit",50)
        self.assertTrue(world.create(1,{1:9}))
        self.assertTrue(world.link(1,1,2,1))
        self.assertTrue(world.resize(2,1,12))
        self.assertEqual(9,world.blobs[world.dirs[1][1]].logical)
        self.assertEqual(12,world.blobs[world.dirs[2][1]].logical)
        self.assertEqual(21,world.report()["allocated_mib"])
        self.assertEqual(0,world.report()["shared_objects"])
        world.verify()

    def test_failed_directory_creation_is_atomic(self):
        for policy in limen.POLICIES:
            world=limen.Domain(policy,20)
            self.assertFalse(world.create(1,{1:10,2:15}))
            self.assertEqual({},world.dirs)
            self.assertEqual({},world.blobs)
            self.assertEqual(20,world.report()["free_mib"])

    def test_fisher_rao_properties_and_invalid_histograms(self):
        a=[.5,.25,.25]
        b=[.25,.5,.25]
        self.assertAlmostEqual(0,limen.Domain.fisher_rao(a,a),places=7)
        self.assertAlmostEqual(limen.Domain.fisher_rao(a,b),
                               limen.Domain.fisher_rao(b,a),places=12)
        self.assertAlmostEqual(math.pi,
                               limen.Domain.fisher_rao([1,0],[0,1]),places=8)
        with self.assertRaises(ValueError):
            limen.Domain.fisher_rao([.2,.2],[.2,.8])
        with self.assertRaises(ValueError):
            limen.Domain.fisher_rao([1],[-1])

    def test_positive_growth_quantile_requires_observation(self):
        world=limen.Domain("limen-sm",100)
        self.assertTrue(world.create(1,{1:5}))
        blob=world.blobs[world.dirs[1][1]]
        self.assertEqual(0,world._reserve(blob))
        blob.growth=[0,2,5,10]
        self.assertEqual(10,world._reserve(blob))
        self.assertIsNotNone(blob.baseline_hist)
        blob.growth=[0,0,0,-1,-2,-3,-4,-5]
        self.assertEqual(0,world._reserve(blob))
        self.assertGreaterEqual(world.model_rebases,1)

    def test_pinned_filler_never_migrates_on_anchor_growth(self):
        world=limen.Domain("limen-sm",40,extent_limit=1)
        self.assertTrue(world.create(1,{1:10}))
        self.assertTrue(world.create(2,{1:3}))
        # Force the anchor/filler adjacency this test is specifically about;
        # do not rely on the optimizer choosing arbitrary coordinates.
        world.blobs[world.dirs[1][1]].extents[0].start=0
        world.blobs[world.dirs[2][1]].extents[0].start=10
        world.verify()
        world.blobs[world.dirs[2][1]].pinned=True
        before=world.report()
        self.assertFalse(world.resize(1,1,13))
        self.assertEqual(before["allocated_mib"],world.report()["allocated_mib"])
        self.assertEqual(0,world.report()["moved_mib"])
        self.assertEqual(10,world.blobs[world.dirs[1][1]].allocated)

    def test_move_small_filler_not_heavy_anchor(self):
        world=limen.Domain("limen-sm",40,extent_limit=1)
        self.assertTrue(world.create(1,{1:10}))
        self.assertTrue(world.create(2,{1:3}))
        world.blobs[world.dirs[1][1]].extents[0].start=0
        world.blobs[world.dirs[2][1]].extents[0].start=10
        world.verify()
        anchor_oid=world.dirs[1][1]
        old_start=world.blobs[anchor_oid].extents[0].start
        self.assertTrue(world.resize(1,1,13))
        self.assertEqual(old_start,world.blobs[anchor_oid].extents[0].start)
        self.assertEqual(3,world.report()["moved_mib"])
        self.assertEqual(1,world.report()["migrations"])
        world.verify()

    def test_ablation_disables_light_filler_relocation(self):
        world=limen.Domain("limen-no-fillers",40,extent_limit=1)
        self.assertTrue(world.create(1,{1:10}))
        self.assertTrue(world.create(2,{1:3}))
        self.assertFalse(world.resize(1,1,13))
        self.assertEqual(0,world.moved)
        world.verify()

    def test_identical_seeded_ideal_requests(self):
        self.assertEqual(limen.common_events(5,80),
                         limen.common_events(5,80))
        self.assertNotEqual(limen.common_events(5,80),
                            limen.common_events(7,80))
        records=limen.run(5,30)
        self.assertEqual(set(limen.POLICIES),
                         {row["policy"] for row in records})
        self.assertTrue(all(row["events"]==50 for row in records))

    def test_failures_split_from_unavailable_dependencies(self):
        world=limen.Domain("spp",capacity=18)
        self.assertFalse(world.create(1,{1:20}))
        self.assertEqual(1,world.rejected)
        self.assertFalse(world.resize(1,1,21))
        self.assertEqual(1,world.blocked)
        self.assertEqual(1,world.rejected)
        self.assertEqual(0,world.accepted)

    def test_reserve_is_revocable_before_real_capacity_rejection(self):
        world=limen.Domain("spp",capacity=55)
        self.assertTrue(world.create(1,{1:10},forecast=40))
        self.assertEqual(18,world.report()["reserved_mib"])
        # Actual 40 MiB request would fail if SPP treated forecast
        # reserves as irrevocably allocated physical content.
        self.assertTrue(world.create(2,{1:40}))
        self.assertEqual(50,world.report()["allocated_mib"])
        self.assertEqual(0,world.report()["reserved_mib"])
        self.assertEqual(18,world.report()["reclaimed_reserve_mib"])
        world.verify()

    def test_failed_real_demand_cannot_publish_reserve_reclamation(self):
        world=limen.Domain("spp",capacity=55)
        self.assertTrue(world.create(1,{1:10},forecast=40))
        before=world.report()
        self.assertFalse(world.create(2,{1:50}))
        after=world.report()
        self.assertEqual(before["reserved_mib"],after["reserved_mib"])
        self.assertEqual(before["allocated_mib"],after["allocated_mib"])
        self.assertEqual(before["reclaimed_reserve_mib"],after["reclaimed_reserve_mib"])
        self.assertNotIn(2,world.dirs)
        world.verify()

    def test_holdout_seed_offsets_are_distinct_and_reproducible(self):
        self.assertEqual(limen.common_events(12,25,"growth-hot"),
                         limen.common_events(12,25,"growth-hot"))
        self.assertNotEqual(limen.common_events(12,25,"growth-hot"),
                            limen.common_events(0,25,"growth-hot"))

    def test_bad_cli_budget_is_rejected(self):
        run=subprocess.run([sys.executable,str(SRC),"--seeds","0"],
                           capture_output=True,text=True,timeout=5)
        self.assertNotEqual(0,run.returncode)
        self.assertIn("invalid bounded research parameters",run.stderr)

    def test_report_marks_simulation_not_hardware(self):
        run=subprocess.run(
            [sys.executable,str(SRC),"--seeds","1","--steps","4"],
            capture_output=True,text=True,timeout=15)
        self.assertEqual(0,run.returncode,run.stderr)
        data=json.loads(run.stdout)
        self.assertIn("RESEARCH_SIMULATION_ONLY",data["status"])
        self.assertEqual(len(limen.POLICIES),len(data["results"]))


if __name__=="__main__":
    unittest.main()
