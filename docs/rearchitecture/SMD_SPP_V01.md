# Sparse Memory Dynamics / Sparse Potential Placement (SMD/SPP) v0.1

**Status: mathematics and independent simulator, not production allocation, optimum proof, 9P VFS, kernel memory or DMA support.**

## Source and architectural ownership

This formalizes the user-supplied 21 November 2026 research note, Testo incollato(2).txt. It proposes a Resource Placement and Lifecycle Manager concerned with total interaction cost over time, not merely fragmentation. The one NXOLMO StateStore/NXREG owns object identity and versions; the kernel/provider owns real resources and capability enforcement; SMD/SPP only proposes where admitted extents might be placed, expanded or migrated. The VFS provides a universal object interface, not an allocator. No shadow state tree, SMD-owned capability catalog, second buddy, second VFS or hardware pointer conversion.

The sample smd_container_t and smd_domain_t from the note are research sketches, NOT an ABI. Adopt the canonical 128-bit NXOLMO object ID, generation, principal and provider lease, checked spans and feature bits instead. A numeric capability_mask hint cannot grant rights. Validate owner/generation/revocation at every protected access and commit.

## Formal state and hard constraints

For container j, W_j is actual allocated storage in a declared domain and interval; G_j is an uncertain nonnegative growth forecast over an explicit horizon, A_j observed activity in an observation window, R_j the OPTIONAL admitted reserve, x_j the abstract placement coordinate. Distinguish logical_size, allocated_size, reserved_size, extent list and active/pin/lease references. An object need not be physically contiguous. q_j = W_j/W_ref + alpha * G_j/G_ref + beta * A_j/A_ref normalizes unlike quantities before combining them.

An illustrative interaction feature U_ij(d) = k q_i q_j / (d+epsilon)^p uses p>0, epsilon>0. Abstract address distance is NOT physical NUMA, SSD, HDD, GPU or CXL distance. Device providers must supply actual topology and access-cost observations. Locality intensity L_ij may counteract repulsion when the same task accesses two resources together. Neither alpha/beta/p nor their performance is derived from theory yet.

The placement search minimizes a candidate policy:

J = lambda1 U_rep + lambda2 C_move + lambda3 C_frag
  + lambda4 C_access + lambda5 C_reserve + lambda6 C_sync
  + lambda7 C_locality + mu ||x(t)-x(t-1)||_1.

Coefficients must be normalized/calibrated, nonnegative and independently tested. The policy is an online local heuristic, never a claim of global optimality. Include robust zero-forecast and deliberately incorrect-forecast cases.

Mandatory admission BEFORE scoring: (1) domain extents are in [L0,L1), aligned and checked for overflow; (2) allocated plus reserved extents do not overlap and fit admitted provider capacity; (3) a pinned, DMA, mapped or actively executed resource cannot be relocated without a real quiesce/remap protocol; (4) no client discovers, reads, moves or retires an object without actual capability+generation; (5) failure leaves old committed state intact.

R_j may be proposed from gamma W_j + delta G_j but remains optional, provider-limited and independently reclaimable. Reserve absence never bypasses hard capacity. The scaling coordinate X_j(t) = a(t) x_j(t) is useful solely for a VIRTUAL planning window; it cannot create RAM or physical address space.

## Incremental lifecycle

On create/resize/access/delete, account resources and validate ownership; first try a bounded fast placement, then schedule optional deferred compaction only if expected benefit exceeds movement/synchronization cost. A free hole can be more useful than immediate compaction. Plan from versioned snapshots, but snapshot data never authorizes an operation.

The only allowed migration state transition is PREPARE -> CHECK_REFERENCES -> RESERVE_DESTINATION -> COPY_OR_REMAP -> VERIFY -> ATOMIC_PUBLISH -> RETIRE_OLD_MAPPING -> WAIT_FOR_GRACE_PERIOD -> RECLAIM. Copy/remap, TLB maintenance, cache coherence, device DMA fences and persistent flush happen only in qualified resource providers. Revocation and cancellation have separate linearization points; RCU/epoch are reclamation primitives, not authority.

## Falsifiable research experiments

The separate research simulator scripts/research/smd_spp_v01.py uses abstract MiB units, a 500 MiB domain and 20 containers with 5–10 files each, seeded create/grow/shrink/delete workload. It compares the exact same trace for First Fit, Best Fit, Worst Fit, and SPP with local repulsion+fragmentation penalties. A Buddy comparison is applicable only for the same guaranteed request/order and honest power-of-two accounting; no performance claim is made from mismatched constraints.

Report total allocated and reserved, free space, largest hole, external fragmentation F_ext = 1 - L_max/F_total (for nonzero F_total), rejected allocations, relocation bytes, migration count and work estimates. Report every seed and policy, including losses. This simulation is NOT a production NXOLMO buffer/page engine, and its placement distance is not measured hardware locality.

Separate experiment B requires actual concurrency 1/2/4/8/16/32 workers with throughput/latency p50/p95/p99, contention and publication races; experiment C tests temporal stability/forecast errors; experiment D tests zero-copy, capability revocation, stale generation, pin/DMA and cross-process access using real providers. No such physical or security result may be inferred from a single-threaded model.

## P4/P3 dependency

SMD may become a POLICY inside a later canonical NXOLMO allocation-domain provider, never a second allocator. Before production admission it requires P1 resource-backed memory and recoverable OOM; P2 per-runtime buffer leases; P3 canonical object capability/MVCC/retire; P4 authenticated REG execution and cancellation. The user-proposed mathematical model therefore contributes research and requirements now, not an unauthorized production security claim.

Source note's optimization is not known to beat Buddy/Slab/extents, and no arbitrary percentage of improvement is declared.

## First reproducible observations, not a victory claim

At 10 fixed seeds with 120 online events per seed, the standalone
500 MiB domain model reported mean fragmentation approximately
0.848 First Fit, 0.857 Best Fit, 0.868 Worst Fit and 0.576 SPP.
Aggregate rejected requests: 424, 422, 430 and **455**
respectively; SPP retained more reserve (843 MiB summed over
10 end states), less application data and fewer moved bytes.
Therefore no unconditional improvement is established. These
are different admitted end states, not matched-equivalent
utilization: report failure rates, usable data volume and
migration costs together before comparing fragmentation. The
simulator intentionally does not claim actual buddy, NUMA, CPU
cycles, concurrency, cryptographic authority or physical DMA.

## Superseded benchmark caveat — 21 November 2026

The first SMD/SPP v0.1 simulator retained forecast reserves
irrevocably, and ranked fragmentation across different admitted
amounts of useful data. The v0.2 comparison corrects that by
reclaiming UNUSED reserves on real demands and representing each
file and its actual segmented extents independently. The v0.1 numbers
must not be used as evidence that SPP is catastrophically inferior.
See LIMEN_SM_V02_VALIDATION.md for the corrected mixed holdout and
independent growth-hot comparison.
