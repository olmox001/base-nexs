# LIMEN-SM versus SMD/SPP — v0.2 scientific comparison

**Status: simulator/research only; NOT a kernel allocator, runtime lease,
real VFS, hardware performance or capability security certification.**

## User source and mathematical requirements

The attached Testo incollato(3).txt (LIMEN-SM v0.1, §§1–16) proposes
a policy for geometric, hierarchical, asynchronous resource placement.
The proposal distinguishes:
- Logical s^L, physically allocated s^A and resident s^R per file:
  these are not interchangeable.
- Directory weights are aggregates, not necessarily one contiguous
  physical interval. Shared hardlink/reflink blocks must be counted
  through a union, not multiplied by the number of references.
- LIMEN's projection Pi(x)=sum(file weights) loses detailed structure:
  retain the per-file references, exact extents, offset and identity
  as hidden state xi, not merely a total directory size.
- A growth quantile R_i(T,eps)=Q_(1-eps)[delta W_i(T)] uses a declared
  observation window and risk. Fisher–Rao distance
  d_FR(p,q)=2 acos(sum sqrt(p*q)) describes distribution DRIFT,
  not direct physical positions or a memory force.
- SPP's V=sum k M_i M_j/(d_ij+delta)^nu is an optional local
  dispersion feature. It cannot create capacity or grant rights.
  Its coefficients are empirical and must never be tuned to make
  a desired experimental outcome pass.
- Heavy anchors and lightweight fillers favor relocating small,
  cold, unpinned blockers rather than moving an entire heavy object.
  Pinned/DMA/active mappings cannot move without a real quiesce protocol.
- V11 probability dynamics are explicitly experimental only;
  the user source does not establish them as an allocation law.

## What was wrong with earlier SMD/SPP v0.1 comparisons

The previous research simulator treated each directory as one contiguous
physical extent, compared fragmentation at different occupancy levels
and could leave speculative reservations irrevocable under real demand.
All three effects can bias a ranking. Retain the v0.1 simulator as a
historical prototype; it is not a sufficient allocator-selection gate.

## v0.2 implementation and comparability rules

scripts/research/limen_sm_v02.py is one standalone scientific experiment,
NOT a second production memory allocator, registry, VFS or capability
table. All compared policies use the SAME:
- 500 abstract MiB domain and per-file segmented extent provider;
- initially 20 directories, each with 5–10 files;
- identical deterministic client-intended create/resize/delete/touch
  events and the same physical capacity/extent limit;
- independent file identity, actual shared-link block deduplication
  and copy-on-write;
- failed-admission rollback with no live partial mutation;
- revocable, unused growth reserves: real demand can reclaim a reserve;
- distinct accepted work, rejected actual requests and blocked requests
  whose prerequisites were previously denied;
- exact allocated, logical, resident, reserved and available space;
- nonoverlap/capacity checks after every successful event.

Seven primary comparison/ablation policies plus one hybrid:
First Fit, Best Fit, Worst Fit, SPP, LIMEN-SM, LIMEN-adaptive,
LIMEN-no-FR and LIMEN-no-fillers. The no-FR variant retains
the SAME relocation algorithm with a fixed margin; the
no-fillers variant retains the Fisher–Rao forecast.
LIMEN-adaptive starts with First Fit and activates statistical
forecasting only after repeated file growth observations.

The operation planner may move small unpinned blockers using an
independent staging copy; a failed attempt cannot modify the live
simulation. All reservation/relocation costs and accepted work must
appear in the report. No numeric potential is used as authorization.

## Required workloads and holdout

- mixed: seeded creates, resizes, deletes and accesses.
- growth-hot: the same initial file distribution, with a few
  persistent hot files receiving repeated growth/shrink samples.
- Holdout seed IDs 10–19 are separate from seed IDs 0–9 used
  during iterative model engineering. Do not refit parameters
  from holdout results.

Measures: successful/rejected/blocked operations, final real data
volume, speculative reserve, reclaimed reserve, largest free span,
external fragmentation, MiB moved, migration count and
scored candidate workload. Scored candidates are NOT CPU cycles,
and observed maximum delay is never WCET.

## Interpretation and limits

An algorithm that reserves 200 MiB and retains 300 MiB data cannot
claim superiority from a lower fragmentation ratio over a policy
that retains 490 MiB of actual data. Report allocation satisfaction,
capacity utilization, reserve, physical relocation and fragmentation
JOINTLY. Prefer Pareto comparisons or explicit policy priorities;
do not declare a global winner without workload assumptions.

The simulator intentionally does NOT model real NUMA/SSD locations,
hardware cache coherency, persistent block flush, DMA pinning,
resource authorization, NXOLMO leases, NXEC HCD, multiprocessor
scheduler latency or actual cross-process concurrency. It also does
not prove full sparse-file offset semantics or complete transactional
VFS operations. Source-level C/QEMU and native hardware tests are
needed before any production placement decision.

## The P0-P4 dependency remains real

SMD/LIMEN must be an optional PLACEMENT POLICY in the ONE actual
NXOLMO/NXEC resource manager. P1 must first expose safe
per-instance provider admission; P2 safe Buffer/page lifetimes;
P3 real StateStore/generation/capability authority; P4
authenticated REG execution and cancellation. The simulator never
supplies missing authority or an alternative VFS.

## Evidence and reruns

The 17+ targeted Python tests exercise real model methods for
Fisher–Rao, hardlinks, COW, bounds, nonoverlap, reserve reclaim,
rollback, reproducibility, and anchor/filler pinning. JSON reports
under ignored build/assurance are raw evidence; a numerical
conclusion is recorded only for runs AFTER the reserve reclaim fix.

## Corrected holdout results, 10 new seeds, mixed workload

After making unused forecast reserves reclaimable, with all policies
receiving the identical 20-directory+120-event holdout trace
(seeds 10 through 19), the v0.2 model measured:

| Policy | Accepted operations (10 traces) | Rejected | Blocked by earlier failures | Mean real allocated MiB | Mean reserve MiB | Mean external fragmentation | Simulated moved MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| First Fit | 1204 | 157 | 39 | 492.2 | 0 | 0.484 | 0 |
| Best Fit | 1201 | 162 | 37 | 494.1 | 0 | 0.429 | 0 |
| Worst Fit | 1205 | 154 | 41 | 488.7 | 0 | 0.754 | 0 |
| SPP (revocable reserve) | 1184 | 204 | 12 | 490.3 | 4.7 | 0.364 | 0 |
| LIMEN-SM | 1202 | 158 | 40 | 492.7 | 1.8 | 0.442 | 4 |
| LIMEN adaptive | 1202 | 155 | 43 | 492.9 | 2.7 | 0.310 | 0 |
| LIMEN without FR | 1203 | 157 | 40 | 493.0 | 1.8 | 0.441 | 4 |
| LIMEN without fillers | 1202 | 154 | 44 | 492.8 | 2.5 | 0.410 | 0 |

**Mixed workload conclusion:** These 10 traces DO NOT support declaring
a general advantage for the more complex LIMEN policy. Worst Fit has
one more completed operation than First Fit, but worse fragmentation;
First Fit's lack of complex scored candidate work and high admitted
useful data make it a defensible low-cost baseline. The v0.1 SPP
penalty was largely an artifact of non-reclaimable reserves: corrected
SPP rises from 805 to 1184 successful holdout operations. However,
it still evaluates 8,718 candidate scores vs First Fit's zero on this
scoring-work counter, which is not a measured CPU time.

These are abstract MiB simulation results, not benchmarks on actual
RAM/NUMA/SSD/hardware memory or kernel paging. Do not compare
fragmentation without considering different occupied capacities.

## Corrected independent growth-hot holdout, seeds 10–19

Each policy received the SAME intended repeated-file-growth workload:
20 initially populated directories, 120 additional events per seed,
the same real domain capacity and individual-file extent limit.
Unused reservations were reclaimable, and failed proposal publication
cannot discard existing data.

| Policy | Accepted operations (10 traces) | Rejected | Real data MiB (mean) | Reserve MiB (mean) | Mean external fragmentation | Simulated moved MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First Fit | 694 | 706 | 451.8 | 0 | 0.210 | 0 |
| Best Fit | 684 | 716 | 450.6 | 0 | 0.205 | 0 |
| Worst Fit | 743 | 657 | 449.2 | 0 | 0.666 | 0 |
| SPP with revocable reserve | 621 | 779 | 431.3 | 33.9 | 0.755 | 0 |
| LIMEN-SM | **1038** | 359 | 487.3 | 1.8 | 0.349 | **801** |
| LIMEN adaptive | 799 | 601 | 462.0 | 3.5 | 0.278 | 167 |
| LIMEN without Fisher–Rao | **1040** | 357 | 491.2 | 0.9 | 0.422 | 976 |
| LIMEN without movable fillers | 757 | 643 | 450.6 | 3.4 | 0.219 | 0 |

The large gain in accepted work is attributable to a capacity-checked
ability to move small unpinned blockers rather than heavy anchors:
the no-fillers ablation falls to 757 admissions. **This benchmark
does NOT establish a consistent admission improvement from Fisher–Rao:**
no-FR admitted two more requests (1040 vs 1038), but moved 175 MiB
more physical data (976 vs 801). That is a useful tradeoff hypothesis,
not a causal/general performance theorem, and no real DMA/NUMA or
processor elapsed cycles were measured.

**Decision:** Retain First Fit/efficient conventional extent
allocation as the default on cold/mixed workloads. Treat bounded
anchor/filler relocation as an OPTIONAL and purely provider-admitted
policy for strongly observed repeated-growth domains. Keep
Fisher–Rao forecasting experimental until independent production
workloads and cost/forecast calibration justify it. Never install the
standalone Python model as kernel code, object authority or a second
allocator, and never expose placement changes without NXOLMO object
generation, permissions and safe lease/quiesce semantics.

The old v0.1 fragmentation advantage of SPP is not a decisive result.
Its v0.2 policy is now compared with identical workloads and
reclaimable reserve; its hot-growth admission rate remains worse
than the specialized LIMEN policy. All numbers above are from
build/assurance/limen-sm-v02-final-growth-hot.json; mixed values
are in build/assurance/limen-sm-v02-final-mixed.json.
