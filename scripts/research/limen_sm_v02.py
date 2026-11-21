#!/usr/bin/env python3
"""LIMEN-SM v0.2 scientific comparator: abstract storage placement, NOT OS code.

All policies operate on identical file-level segmented extents, generated client
requests, and physical capacity. Failed dependencies are separated from failed
admissions. No performance/security/kernel/VFS or physical-locality claims.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import random
from dataclasses import dataclass, field
from pathlib import Path

POLICIES = ("first-fit", "best-fit", "worst-fit", "spp",
            "limen-sm", "limen-adaptive", "limen-no-fr",
            "limen-no-fillers")
HISTORY = 12


@dataclass
class Extent:
    start: int
    length: int


@dataclass
class Blob:
    logical: int
    allocated: int
    extents: list[Extent]
    refs: int = 1
    pinned: bool = False
    resident: int = 0
    access: int = 0
    growth: list[int] = field(default_factory=list)
    baseline_hist: list[float] | None = None

    @property
    def capacity(self) -> int:
        return sum(e.length for e in self.extents)


@dataclass
class Domain:
    policy: str
    capacity: int = 500
    extent_limit: int = 4  # Experimental provider admission, same for all.
    dirs: dict[int, dict[int, int]] = field(default_factory=dict)
    blobs: dict[int, Blob] = field(default_factory=dict)
    sequence: int = 0
    accepted: int = 0
    rejected: int = 0
    blocked: int = 0
    moved: int = 0
    migrations: int = 0
    reclaimed: int = 0
    candidates: int = 0
    model_rebases: int = 0

    def __post_init__(self):
        if (self.policy not in POLICIES or self.capacity <= 0
                or self.extent_limit <= 0):
            raise ValueError("invalid finite provider domain")

    def extents(self) -> list[Extent]:
        return sorted((e for b in self.blobs.values() for e in b.extents),
                      key=lambda e: (e.start, e.length))

    def holes(self) -> list[tuple[int, int]]:
        end = 0
        free = []
        for e in self.extents():
            if e.start < end or e.length <= 0 or e.start + e.length > self.capacity:
                raise ValueError("overlapping or invalid extents")
            if e.start > end:
                free.append((end, e.start))
            end = e.start + e.length
        if end < self.capacity:
            free.append((end, self.capacity))
        return free

    def verify(self) -> None:
        occupied = 0
        for oid, blob in self.blobs.items():
            refs = sum(oid == key for entries in self.dirs.values()
                       for key in entries.values())
            if (refs != blob.refs or refs <= 0 or blob.logical < 0
                    or blob.allocated <= 0 or blob.allocated > blob.logical
                    or blob.allocated > blob.capacity or blob.resident > blob.allocated
                    or blob.resident < 0 or len(blob.extents) > self.extent_limit):
                raise ValueError("invalid per-file information/ownership")
            occupied += blob.capacity
        if occupied + sum(hi - lo for lo, hi in self.holes()) != self.capacity:
            raise ValueError("resource capacity not conserved")

    @staticmethod
    def _fragmentation(holes: list[tuple[int, int]]) -> float:
        total = sum(y-x for x, y in holes)
        return 0.0 if total == 0 else 1 - max((y-x for x, y in holes), default=0)/total

    @staticmethod
    def _growth_hist(samples: list[int]) -> list[float]:
        bins = [0.5, 0.5, 0.5, 0.5]
        for sample in samples:
            idx = 0 if sample <= 0 else 1 if sample <= 2 else 2 if sample <= 5 else 3
            bins[idx] += 1.0
        total = sum(bins)
        return [x/total for x in bins]

    @staticmethod
    def fisher_rao(before: list[float], after: list[float]) -> float:
        if len(before) != len(after) or not before or any(
                x < 0 for x in before + after):
            raise ValueError("invalid probability distributions")
        if abs(sum(before)-1) > 1e-8 or abs(sum(after)-1) > 1e-8:
            raise ValueError("distributions not normalized")
        similarity = sum(math.sqrt(a*b) for a, b in zip(before, after))
        return 2*math.acos(max(0., min(1., similarity)))

    def _reserve(self, blob: Blob) -> int:
        if self.policy == "spp":
            return int(.2*blob.allocated + .4*max(blob.growth, default=0))
        if not self.policy.startswith("limen"):
            return 0
        if self.policy == "limen-no-fr":
            return int(.2*blob.allocated)
        observed = blob.growth[-HISTORY:]
        if len(observed) < 3:
            return 0
        if (self.policy == "limen-adaptive" and
                sum(delta > 0 for delta in observed) < 3):
            return 0
        hist = self._growth_hist(observed)
        if blob.baseline_hist is None:
            blob.baseline_hist = hist
        elif self.fisher_rao(blob.baseline_hist, hist) > .45:
            blob.baseline_hist = hist
            self.model_rebases += 1
        # Empirical upper quantile of positive *observed* changes.
        positive = sorted(x for x in observed if x > 0)
        if not positive:
            return 0
        pos = max(0, math.ceil(.9*len(positive))-1)
        return positive[pos]

    def _placement_score(self, start: int, length: int,
                         owner: int | None = None) -> float:
        self.candidates += 1
        holes = self.holes()
        hypothetical = []
        for lo, hi in holes:
            if lo <= start and start + length <= hi:
                if lo < start:
                    hypothetical.append((lo, start))
                if start+length < hi:
                    hypothetical.append((start+length, hi))
            else:
                hypothetical.append((lo, hi))
        if self.policy in ("first-fit", "best-fit", "worst-fit"):
            return 0.
        if self.policy == "limen-adaptive":
            blob = self.blobs.get(owner)
            # A new or cold file uses ordinary First Fit, avoiding
            # speculative geometric costs. Repeated growth unlocks the
            # statistical policy; absent evidence the simpler rule wins.
            if blob is None or sum(x > 0 for x in blob.growth) < 3:
                return float(start)
        potential = 0.
        if self.policy != "limen-no-fillers":
            for other_oid, other in self.blobs.items():
                if other_oid == owner:
                    continue
                activity = (1 + min(other.access, 20)/20)
                for e in other.extents:
                    distance = abs(start+length/2 - e.start-e.length/2)
                    potential += (length/self.capacity)*(e.length/self.capacity)*activity/(distance+1)
        return potential + .35*self._fragmentation(hypothetical)

    def _allocate(self, size: int, owner: int | None = None,
                  forbidden: tuple[int, int] | None = None) -> list[Extent] | None:
        if size < 0:
            return None
        allocated = []
        # Candidate holes are computed with temporary segments checked against
        # the current committed layout, without actually publishing them.
        while size > 0 and len(allocated) < self.extent_limit:
            # Auxiliary planning view: never alter the live source objects.
            all_holes = self.holes()
            for e in allocated:
                altered = []
                for lo, hi in all_holes:
                    if e.start >= hi or e.start+e.length <= lo:
                        altered.append((lo, hi))
                    else:
                        if lo < e.start:
                            altered.append((lo, e.start))
                        if e.start+e.length < hi:
                            altered.append((e.start+e.length, hi))
                all_holes = altered
            options = []
            for lo, hi in all_holes:
                spans = [(lo,hi)] if forbidden is None else [
                    (lo,min(hi,forbidden[0])),(max(lo,forbidden[1]),hi)]
                for start, stop in spans:
                    if stop <= start:
                        continue
                    length = min(size, stop-start)
                    if self.policy == "first-fit":
                        order = (start, start)
                    elif self.policy == "best-fit":
                        order = (stop-start, start)
                    elif self.policy == "worst-fit":
                        order = (-(stop-start), start)
                    else:
                        picks = (start, stop-length, start+(stop-start-length)//2)
                        order = min((self._placement_score(x, length, owner), x)
                                    for x in picks)
                    options.append((*order, -length, Extent(order[1], length)))
            if not options:
                return None
            selection = min(options, key=lambda row: row[:3])[3]
            allocated.append(selection)
            size -= selection.length
        return allocated if size == 0 else None

    def _move_light_fillers(self, anchor_oid: int, needed: int) -> bool:
        """Plan a bounded selective move before a heavy anchor grows.

        Callers operate on a PRIVATE staging copy. On failure the parent
        refuses the request and discards this graph; no live mutation.
        """
        if self.policy not in ("limen-sm", "limen-adaptive",
                                "limen-no-fr") or needed <= 0:
            return False
        anchor = self.blobs[anchor_oid]
        if not anchor.extents or anchor.pinned or anchor.allocated < 6:
            return False
        tail = max(anchor.extents, key=lambda e: e.start+e.length)
        begin = tail.start + tail.length
        end = begin + needed
        if end > self.capacity:
            return False
        blockers = []
        for oid, other in self.blobs.items():
            if oid == anchor_oid:
                continue
            for part in other.extents:
                if part.start < end and part.start+part.length > begin:
                    blockers.append((oid, part))
        if not blockers or len(blockers) > 8:
            return False
        moved_bytes = sum(part.length for _, part in blockers)
        if (moved_bytes > 2*needed or
                any(self.blobs[oid].pinned or
                    part.length > max(3, anchor.allocated//2)
                    for oid, part in blockers)):
            return False
        for oid, part in blockers:
            other = self.blobs[oid]
            other.extents.remove(part)
            replacements = self._allocate(part.length, oid, (begin, end))
            if (replacements is None or
                    len(other.extents)+len(replacements) > self.extent_limit):
                return False
            other.extents.extend(replacements)
        if not any(lo <= begin and end <= hi for lo, hi in self.holes()):
            return False
        tail.length += needed
        self.moved += moved_bytes
        self.migrations += len(blockers)
        return True

    def _reclaim_optional_reserves(self, exclude: int | None = None) -> int:
        """Reclaim revocable UNUSED tail extents before denying real demand.

        A reserve is never physical ownership that outranks a real
        admitted write. This function runs ONLY on a private plan, so
        a later allocation failure discards the whole proposed revocation.
        """
        total = 0
        order = sorted(self.blobs,
                       key=lambda oid: -(self.blobs[oid].capacity-
                                         self.blobs[oid].allocated))
        for oid in order:
            if oid == exclude:
                continue
            blob = self.blobs[oid]
            spare = blob.capacity-blob.allocated
            total += spare
            while spare > 0:
                last = blob.extents[-1]
                reduce = min(spare,last.length)
                last.length -= reduce
                spare -= reduce
                if last.length == 0:
                    blob.extents.pop()
        self.reclaimed += total
        self.verify()
        return total

    def _commit_from(self, staged: Domain) -> None:
        # Entire validated planning graph is atomically replaced in the
        # SINGLE-THREADED SIMULATION. Not a production MVCC proof.
        staged.verify()
        for attribute in ("dirs", "blobs", "sequence", "moved", "migrations",
                          "reclaimed", "candidates", "model_rebases"):
            setattr(self, attribute, getattr(staged, attribute))
        self.accepted += 1

    def create(self, cid: int, files: dict[int, int], forecast: int = 0) -> bool:
        if cid in self.dirs or not files or any(size <= 0 for size in files.values()):
            self.rejected += 1
            return False
        staged = copy.deepcopy(self)
        staged.dirs[cid] = {}
        for fid, size in files.items():
            oid = staged.sequence + 1
            staged.sequence = oid
            blob = Blob(logical=size, allocated=size, extents=[], growth=[forecast])
            desired = staged._reserve(blob)
            allocated = staged._allocate(size + desired, oid)
            if allocated is None:
                allocated = staged._allocate(size, oid)
            if allocated is None:
                staged._reclaim_optional_reserves()
                allocated = staged._allocate(size, oid)
            if allocated is None:
                self.rejected += 1
                self.candidates += staged.candidates - self.candidates
                return False
            blob.extents = allocated
            staged.blobs[oid] = blob
            staged.dirs[cid][fid] = oid
        self._commit_from(staged)
        return True

    def resize(self, cid: int, fid: int, new_size: int) -> bool:
        oid = self.dirs.get(cid, {}).get(fid)
        if oid is None:
            self.blocked += 1
            return False
        if new_size <= 0:
            self.rejected += 1
            return False
        staged = copy.deepcopy(self)
        staged_oid = staged.dirs[cid][fid]
        blob = staged.blobs[staged_oid]
        if blob.refs > 1:
            # File-level copy-on-write, never writes through hard-linked alias.
            allocated = staged._allocate(blob.allocated)
            if allocated is None:
                staged._reclaim_optional_reserves(exclude=staged_oid)
                allocated = staged._allocate(blob.allocated)
            if allocated is None:
                self.rejected += 1
                return False
            blob.refs -= 1
            staged.sequence += 1
            staged_oid = staged.sequence
            blob = copy.deepcopy(blob)
            blob.refs = 1
            blob.extents = allocated
            staged.blobs[staged_oid] = blob
            staged.dirs[cid][fid] = staged_oid
            staged.moved += blob.allocated
        old = blob.allocated
        delta = new_size-old
        blob.growth.append(delta)
        blob.growth = blob.growth[-HISTORY:]
        if delta > 0:
            desired = max(0, staged._reserve(blob))
            extra = max(0, new_size + desired-blob.capacity)
            if extra > 0:
                # Attempt extension of last extent before introducing a new one.
                for extent in sorted(blob.extents, key=lambda e:e.start):
                    adjacent = next((hi-lo for lo, hi in staged.holes()
                                     if lo == extent.start+extent.length), 0)
                    increase = min(adjacent, extra)
                    if increase > 0:
                        extent.length += increase
                        extra -= increase
                    if extra == 0:
                        break
            if extra > 0:
                # Only consider moving small fillers for ACTUAL demand, never
                # to manufacture optional reserve. Same max-extents provider.
                real_need = max(0, new_size-blob.capacity)
                if (real_need > 0 and
                        len(blob.extents) >= staged.extent_limit):
                    # Test relocation in an independent transaction workspace.
                    # Failed partial moves must NEVER leak into fallback layout.
                    moved_plan = copy.deepcopy(staged)
                    if moved_plan._move_light_fillers(staged_oid, real_need):
                        staged = moved_plan
                        blob = staged.blobs[staged_oid]
                        extra = max(0, new_size-blob.capacity)
                segments = staged._allocate(extra, staged_oid) if extra > 0 else []
                if (segments is None or
                        len(blob.extents)+len(segments) > staged.extent_limit):
                    needed = max(0, new_size-blob.capacity)
                    segments = staged._allocate(needed, staged_oid) if needed else []
                if (segments is None or
                        len(blob.extents)+len(segments) > staged.extent_limit):
                    staged._reclaim_optional_reserves(exclude=staged_oid)
                    needed = max(0, new_size-blob.capacity)
                    segments = staged._allocate(needed, staged_oid) if needed else []
                if (segments is None or
                        len(blob.extents)+len(segments) > staged.extent_limit):
                    self.rejected += 1
                    return False
                blob.extents.extend(segments)
        else:
            # Keep any physically admitted reserve in SPP and LIMEN;
            # ordinary baselines give tail extents back to the provider.
            if self.policy in ("first-fit", "best-fit", "worst-fit"):
                release = blob.capacity-new_size
                while release > 0 and blob.extents:
                    last = blob.extents[-1]
                    drop = min(last.length, release)
                    last.length -= drop
                    release -= drop
                    if last.length == 0:
                        blob.extents.pop()
        blob.logical = new_size
        blob.allocated = new_size
        blob.resident = min(blob.resident, new_size)
        if not blob.extents or blob.capacity < new_size:
            self.rejected += 1
            return False
        self._commit_from(staged)
        return True

    def unlink(self, cid: int, fid: int) -> bool:
        oid = self.dirs.get(cid, {}).get(fid)
        if oid is None:
            self.blocked += 1
            return False
        staged = copy.deepcopy(self)
        del staged.dirs[cid][fid]
        blob = staged.blobs[oid]
        blob.refs -= 1
        if blob.refs == 0:
            del staged.blobs[oid]
        if not staged.dirs[cid]:
            del staged.dirs[cid]
        self._commit_from(staged)
        return True

    def link(self, from_cid: int, from_fid: int, to_cid: int, to_fid: int) -> bool:
        oid = self.dirs.get(from_cid, {}).get(from_fid)
        if oid is None or to_fid in self.dirs.get(to_cid, {}):
            self.blocked += 1
            return False
        staged = copy.deepcopy(self)
        staged.dirs.setdefault(to_cid, {})[to_fid] = oid
        staged.blobs[oid].refs += 1
        self._commit_from(staged)
        return True

    def touch(self, cid: int, fid: int, read_mib: int = 1) -> bool:
        oid = self.dirs.get(cid, {}).get(fid)
        if oid is None:
            self.blocked += 1
            return False
        blob = self.blobs[oid]
        blob.access += 1
        blob.resident = min(blob.allocated, max(blob.resident, read_mib))
        self.accepted += 1
        self.verify()
        return True

    def report(self) -> dict:
        self.verify()
        holes = self.holes()
        allocated = sum(b.allocated for b in self.blobs.values())
        logical = sum(b.logical for b in self.blobs.values())
        reserved = sum(b.capacity-b.allocated for b in self.blobs.values())
        return {
            "policy":self.policy, "capacity_mib":self.capacity,
            "logical_mib":logical, "allocated_mib":allocated,
            "resident_mib":sum(b.resident for b in self.blobs.values()),
            "reserved_mib":reserved,
            "free_mib":self.capacity-allocated-reserved,
            "largest_hole_mib":max((hi-lo for lo,hi in holes),default=0),
            "external_fragmentation":round(self._fragmentation(holes),6),
            "accepted":self.accepted,"rejected":self.rejected,
            "blocked_dependency":self.blocked,"migrations":self.migrations,
            "moved_mib":self.moved,"reclaimed_reserve_mib":self.reclaimed,
            "candidate_work":self.candidates,
            "model_rebases":self.model_rebases, "physical_extents":len(self.extents()),
            "individual_files":sum(len(items) for items in self.dirs.values()),
            "shared_objects":sum(b.refs>1 for b in self.blobs.values())}

def common_events(seed:int,steps:int=120,
                  workload:str="mixed") -> list[tuple]:
    """One trace of ideal caller intentions, independent of actual policies.

    'growth-hot' targets a small stable set of files repeatedly so Fisher–
    Rao and high-quantile forecasts receive enough *real observations*.
    """
    rng=random.Random(seed)
    ideal:dict[int,dict[int,int]] = {}
    operations = []
    for cid in range(20):
        files={fid:rng.randint(1,3) for fid in range(rng.randint(5,10))}
        ideal[cid]=files.copy()
        operations.append(("create",cid,files,rng.randint(0,8)))
    new_cid=20
    hot=[(cid,min(ideal[cid])) for cid in (0,3,8,13)]
    for _ in range(steps):
        kind=rng.randrange(100)
        if workload=="growth-hot" and kind < 75:
            cid,fid=rng.choice(hot)
            previous=ideal[cid][fid]
            # Strong bursts plus occasional shrink for detectable drift.
            new_size=max(1,previous+
                         (rng.randint(1,5) if kind < 59 else
                          -rng.randint(1,3)))
            ideal[cid][fid]=new_size
            operations.append(("resize",cid,fid,new_size))
            continue
        cid=rng.choice(list(ideal))
        fid=rng.choice(list(ideal[cid]))
        if kind < 54:
            new_size=rng.randint(1,12)
            ideal[cid][fid]=new_size
            operations.append(("resize",cid,fid,new_size))
        elif kind < 75:
            operations.append(("touch",cid,fid,rng.randint(1,5)))
        elif (kind < 91 and len(ideal[cid]) > 1 and
              (workload != "growth-hot" or (cid,fid) not in hot)):
            del ideal[cid][fid]
            operations.append(("unlink",cid,fid,0))
        else:
            files={j:rng.randint(1,3) for j in range(rng.randint(5,10))}
            ideal[new_cid]=files.copy()
            operations.append(("create",new_cid,files,rng.randint(0,8)))
            new_cid+=1
    return operations

def run(seed:int,steps:int=120,capacity:int=500,
        workload:str="mixed") -> list[dict]:
    events=common_events(seed,steps,workload)
    records=[]
    for policy in POLICIES:
        domain=Domain(policy,capacity)
        for kind,cid,a,b in events:
            if kind=="create":
                domain.create(cid,a,b)
            elif kind=="resize":
                domain.resize(cid,a,b)
            elif kind=="unlink":
                domain.unlink(cid,a)
            else:
                domain.touch(cid,a,b)
        records.append({"seed":seed,"events":len(events),**domain.report()})
    return records

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seeds",type=int,default=10)
    ap.add_argument("--seed-start",type=int,default=0)
    ap.add_argument("--steps",type=int,default=120)
    ap.add_argument("--capacity",type=int,default=500)
    ap.add_argument("--workload",choices=("mixed","growth-hot"),
                    default="mixed")
    ap.add_argument("--output",default="-")
    opts=ap.parse_args()
    if (opts.seeds < 1 or opts.seeds > 200 or
            opts.seed_start < 0 or opts.seed_start > 100000 or
            opts.steps < 0 or opts.steps > 20000 or
            opts.capacity < 1 or opts.capacity > 10000000):
        ap.error("invalid bounded research parameters")
    results=[entry for seed in range(opts.seed_start,opts.seed_start+opts.seeds)
             for entry in run(seed,opts.steps,opts.capacity,opts.workload)]
    document={"status":"RESEARCH_SIMULATION_ONLY_NOT_PRODUCTION_OR_SECURITY",
              "workload":opts.workload,
              "methodology":"identical ideal event trace, file extent provider, capacity",
              "domain":"abstract MiB; not measured physical distance",
              "policies":POLICIES,"results":results}
    result=json.dumps(document,indent=2)+"\n"
    if opts.output=="-":
        print(result,end="")
    else:
        Path(opts.output).write_text(result,encoding="utf-8")

if __name__=="__main__":
    main()
