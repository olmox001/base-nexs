#!/usr/bin/env python3
"""Pure Python SMD/SPP research simulator. NOT an OS allocator or a security gate.

All algorithms see the same seeded workload over a simulated 500 MiB domain.
Positions are abstract, not NUMA/SSD/HDD physical distances.
"""
from __future__ import annotations
import argparse
import json
import random
from dataclasses import dataclass, field


@dataclass
class Container:
    files: dict[int, int]
    start: int
    span: int
    forecast: int = 0
    pinned: bool = False
    moves: int = 0

    @property
    def size(self):
        return sum(self.files.values())


@dataclass
class Layout:
    policy: str
    capacity: int = 500
    live: dict[int, Container] = field(default_factory=dict)
    rejected: int = 0
    bytes_moved: int = 0
    total_migrations: int = 0
    candidates: int = 0

    def __post_init__(self):
        if self.policy not in ("first-fit", "best-fit", "worst-fit", "spp"):
            raise ValueError("unsupported research policy")
        if self.capacity <= 0:
            raise ValueError("invalid domain capacity")

    def holes(self):
        occupied = sorted((c.start, c.start + c.span) for c in self.live.values())
        result, cursor = [], 0
        for start, end in occupied:
            if start < cursor or end > self.capacity:
                raise ValueError("overlap or out-of-range extent")
            if cursor < start:
                result.append((cursor, start))
            cursor = end
        if cursor < self.capacity:
            result.append((cursor, self.capacity))
        return result

    @staticmethod
    def fragmentation(holes):
        sizes = [hi - lo for lo, hi in holes]
        return 0.0 if not sizes else 1.0 - max(sizes) / sum(sizes)

    def score(self, start, span, logical, forecast):
        after = []
        for lo, hi in self.holes():
            if lo <= start and start + span <= hi:
                if lo < start:
                    after.append((lo, start))
                if start + span < hi:
                    after.append((start + span, hi))
            else:
                after.append((lo, hi))
        potential = 0.0
        q = (logical + forecast) / self.capacity
        for other in self.live.values():
            d = abs(start + span/2 - other.start - other.span/2)
            potential += q * ((other.size + other.forecast) / self.capacity) / (d+1)
        return potential + 0.35*self.fragmentation(after) + 0.05*(span-logical)/self.capacity

    def placement(self, logical, forecast=0):
        if logical <= 0 or logical > self.capacity or forecast < 0:
            return None
        reserve = int(.20*logical + .40*forecast) if self.policy == "spp" else 0
        holes = self.holes()
        spans = (logical + reserve, logical) if reserve else (logical,)
        options = []
        span = logical
        for length in spans:
            options = [(lo, hi) for lo, hi in holes if hi-lo >= length]
            if options:
                span = length
                break
        if not options:
            return None
        if self.policy == "first-fit":
            return min(lo for lo, _ in options), span
        if self.policy == "best-fit":
            return min(options, key=lambda h:(h[1]-h[0],h[0]))[0], span
        if self.policy == "worst-fit":
            return max(options, key=lambda h:(h[1]-h[0],-h[0]))[0], span
        scores = []
        for lo, hi in options:
            for start in (lo, hi-span, lo+(hi-lo-span)//2):
                self.candidates += 1
                scores.append((self.score(start,span,logical,forecast),start))
        return min(scores)[1], span

    def create(self, cid, files, forecast=0):
        if cid in self.live or not files or any(v<=0 for v in files.values()):
            self.rejected += 1
            return False
        result = self.placement(sum(files.values()),forecast)
        if result is None:
            self.rejected += 1
            return False
        self.live[cid] = Container(dict(files),result[0],result[1],forecast)
        self.verify()
        return True

    def resize(self,cid,fid,new):
        item=self.live.get(cid)
        if item is None or fid not in item.files or new<=0:
            self.rejected += 1
            return False
        size=item.size-item.files[fid]+new
        if size > self.capacity:
            self.rejected += 1
            return False
        if size<=item.span:
            item.files[fid]=new
            if self.policy!="spp":
                item.span=size
            self.verify()
            return True
        end=item.start+item.span
        free_after=next((hi-lo for lo,hi in self.holes() if lo==end),0)
        if size-item.span<=free_after:
            item.files[fid]=new
            item.span=size
            self.verify()
            return True
        if item.pinned:
            self.rejected += 1
            return False
        proposed=self.placement(size,item.forecast)
        if proposed is None:
            self.rejected += 1
            return False
        self.bytes_moved+=item.size
        self.total_migrations+=1
        item.moves+=1
        item.start,item.span=proposed
        item.files[fid]=new
        self.verify()
        return True

    def delete(self,cid,fid):
        item=self.live.get(cid)
        if item is None or fid not in item.files:
            self.rejected+=1
            return False
        del item.files[fid]
        if not item.files:
            del self.live[cid]
        elif self.policy!="spp":
            item.span=item.size
        self.verify()
        return True

    def verify(self):
        used=0
        for item in self.live.values():
            if (item.start<0 or item.size<=0 or item.size>item.span or
                item.start+item.span>self.capacity or
                any(v<=0 for v in item.files.values())):
                raise ValueError("invalid resource admission")
            used+=item.span
        if used+sum(hi-lo for lo,hi in self.holes())!=self.capacity:
            raise ValueError("lost or overlapping space")

    def report(self):
        self.verify()
        holes=self.holes()
        logical=sum(c.size for c in self.live.values())
        reserved=sum(c.span-c.size for c in self.live.values())
        return dict(policy=self.policy,logical_mib=logical,
                    reserved_mib=reserved,free_mib=self.capacity-logical-reserved,
                    largest_free_mib=max((hi-lo for lo,hi in holes),default=0),
                    external_fragmentation=round(self.fragmentation(holes),6),
                    migrations=self.total_migrations,
                    moved_mib=self.bytes_moved,rejected=self.rejected,
                    scored_candidates=self.candidates)


def common_trace(seed,steps=120):
    randomizer=random.Random(seed)
    populations={}
    trace=[]
    for cid in range(20):
        files={n:randomizer.randint(1,3)
               for n in range(randomizer.randint(5,10))}
        populations[cid]=dict(files)
        trace.append(("create",cid,files,randomizer.randint(0,8)))
    nextid=20
    for _ in range(steps):
        cid=randomizer.choice(list(populations))
        fid=randomizer.choice(list(populations[cid]))
        action=randomizer.randrange(100)
        if action<68:
            size=randomizer.randint(1,10)
            populations[cid][fid]=size
            trace.append(("resize",cid,fid,size))
        elif action<88 and len(populations[cid])>1:
            del populations[cid][fid]
            trace.append(("delete",cid,fid,0))
        else:
            files={n:randomizer.randint(1,3)
                   for n in range(randomizer.randint(5,10))}
            populations[nextid]=dict(files)
            trace.append(("create",nextid,files,randomizer.randint(0,8)))
            nextid+=1
    return trace


def experiment(seed,steps=120):
    trace=common_trace(seed,steps)
    reports=[]
    for policy in ("first-fit","best-fit","worst-fit","spp"):
        layout=Layout(policy)
        for action,cid,a,b in trace:
            if action=="create":
                layout.create(cid,a,b)
            elif action=="resize":
                layout.resize(cid,a,b)
            else:
                layout.delete(cid,a)
        reports.append({"seed":seed,"events":len(trace),**layout.report()})
    return reports


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds",type=int,default=10)
    parser.add_argument("--steps",type=int,default=120)
    parser.add_argument("--output",default="-")
    args=parser.parse_args()
    if not 1<=args.seeds<=200 or not 0<=args.steps<=20000:
        parser.error("invalid finite experiment budget")
    report={"status":"SIMULATION_ONLY_NO_HARDWARE_OR_SECURITY_PROOF",
            "domain":"abstract-MiB",
            "reports":[row for seed in range(args.seeds)
                       for row in experiment(seed,args.steps)]}
    result=json.dumps(report,indent=2)+"\n"
    if args.output=="-":
        print(result,end="")
    else:
        from pathlib import Path
        Path(args.output).write_text(result,encoding="utf-8")


if __name__=="__main__":
    main()
