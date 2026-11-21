# P3/P5 — One named NXOLMO VFS object and dynamic session channels

Date: 2026-11-21. STATUS: verified hosted cutover of one legacy VFS
backend, not a complete P3/P4/P5 exit. Governing plan:
END_TO_END_REDESIGN_PLAN.md.

## Implemented in the actual product, no parallel new inode index

The legacy kernel/vfs.c enumerated all prior inode sequence numbers and
cloned registry Values looking for a pathname. This repeated O(number of
historical inodes) work for open/stat/create and published both a copied
path and mode under /vfs/inode/N.

The replacement uses the **single NXOLMO StateStore object** exposed through
the existing RegKey name projection /vfs/files/<user-path>. Its nx_object_ref
is the resource's only identity. A file stores its Value payload in the
same node: one successful write changes **that file's actual revision**.
There is no /vfs/inode/N scan, numeric copy, /@data shadow node or invented
Qid: 9P takes object ID and version from the exact canonical ref/revision.

The VFS FD is a session-local structure with canonical ref, name,
mode and offset. Before I/O it checks that its StateStore object is
still live and that the current named node matches its generation.
Delete/recreate cannot resurrect an old FD. The original static 64-FD
array is replaced with growable provider-page storage. Likewise the
old static 64-FID table is replaced with provider-backed dynamic
session slots. The 9P FID stores its canonical NXOLMO ref; walk/open
validate its path; read/write check the ref directly in StateStore
and then through the actual VFS FD, without redoing an unnecessary
stat(path) before each operation.

Existing syscall remove now calls the actual named VFS remove and reports
its real success/failure instead of deleting an arbitrary registry path
and returning a fabricated success.

## Real source-backed acceptance tests

- tests/test_full_9p_vfs_roundtrip.c exercises actual runtime startup,
  Tattach/Twalk/Topen/Twrite/Tread/Tclunk, malformed and unauthorized requests,
  independent FIDs, offsets, wire Qids and exact original StateStore
  revisions, including the Qid version after a subsequent write.
- 2,400 named files are created through actual vfs_open and StateStore,
  192 distinct live file descriptors are held concurrently and closed,
  and 8,192 actual random vfs_stat calls verify IDs and modes.
  No /vfs/inode root is created. Deleting a file and recreating it
  invalidates the previous FD and allocates a different canonical ref.
- 192 independent live 9P FIDs are walked and opened simultaneously,
  each read through actual VFS FDs and closed. Stale FIDs cannot access
  a recreated pathname. The original StateStore validate check passes.
- 2,000 write commits + 2,000 independent wire readbacks, per-operation
  StateStore identity/revision checking; 2,048 hostile 9P requests
  remain rejected. Long-running 1,000,000 full-commit and hostile
  suites retain independent measured log files.
- make test-system-syscall-fuzz covers every current 21 hosted
  dispatcher verbs across actual direct, wire and REG IPC paths.

## Requalified real integrated results

- make power12-phase0: PASS, 29 source/catalog and ownership assertions.
- make test -j4: PASS, including 33 NEXS syntax sources, 17
  interpreter examples, all hosted functional and hostile tests.
- make test-9p-e2e-sanitizers: PASS on actual product ASan/UBSan build,
  1,000 real commits/readbacks, 10,000 random hostile requests,
  2,400 object names and 192 simultaneous FD/FID lease-like slots.
- make test-baremetal-qemu-interactive: PASS for both independent
  AMD64 and ARM64 QEMU guests. Four UART REPL calculations are
  actually executed and outputs checked on each.
- make perf-p9-full-commits NX_P9_FULL_COMMITS=1000000:
  1,000,000 actual TWRITE commits with each file-object revision
  checked, plus 1,000,000 TREAD content comparisons.
  One observed hosted run: 10,632.3 ns mean write,
  3,484.3 ns mean read, **135,085.1 full operations/s**
  including generator and all StateStore checks.
- This is a single measured host run, NOT a controlled paired
  performance experiment; earlier performance numbers are not
  directly comparable without repetition and workload control.

Evidence: build/assurance/p3_vfs_single_object_{phase0,
full_test,sanitizer,dual_qemu,1m_commits}.log. Build outputs stay
outside version control.

## Direct CPU profile during the real million-syscall campaign

A real macOS profiler sampled the *actual running* hosted
system_syscall_fuzz process PID 24523 for 3 seconds at 1 ms sampling
interval. Raw evidence (not committed; host build output):
build/assurance/p3_named_vfs_live_stack_sample.txt.

The live call graph repeatedly includes:

- sys_write -> vfs_write -> reg_get -> val_clone -> buddy_strdup ->
  nexs_buddy_arena_alloc;
- vfs_write -> nexs_alloc -> buddy_alloc;
- vfs_write -> val_str -> buddy_strdup;
- vfs_write -> reg_set -> val_clone_checked -> val_clone -> buddy_strdup;
- sys_chdir -> reg_set -> val_clone_checked -> buddy_strdup.

These are measured *actual* call stacks. They demonstrate that the
current string-backed file write path still copies and allocates multiple
times, even though its identity and revision now belong to one NXOLMO
file object. No current source can truthfully claim zero-copy file data.
The correct migration is an NXOLMO page/buffer lease with validated
range/capability, in-place mutation and a single checked revision commit.
Do not add a second file/data registry or a parallel pointer cache.

The 3-second profiler perturbs the long fuzz run; any before/after
throughput comparison based on these runs remains exploratory, not a
rigorously controlled performance study. No exact time attribution to
one call site is inferred from these qualitative stack fragments.

## Requalified full-system syscall telemetry after cutover

The 1,000,000-random-request hosted production dispatcher campaign
(with 63 additional real preflight dispatches) plus 250,000 9P
requests completed with zero assertion failures. Actual wall time
was 425.088 s, versus 665.488 s for the same seed and exact
per-verb counts on the prior numeric inode backend.
The full 63-operation table, buddy/page statistics and verification
are in P8_NAMED_VFS_SYSCALL_PROFILE_2026_11_21.md.

Observed mean native direct stat 1,244.87 us -> 203.52 us
(6.12x ratio), native open 1,014.73 us -> 151.27 us
(6.71x ratio), and REG_IPC stat 1,928.35 us -> 824.62 us
(2.34x ratio). The macrophase timings come from two independent
Mac hosted runs, one with CPU sampling, and are not a rigorous
paired/multirun performance proof. No guest NXEC/SMP path is
claimed.

## Strict Power12 remediation in actual VFS and 9P production source

Full strict Power12 originally reported 255 blocking diagnostics.
After these changes it reports **242**. The 3 production break
statements in the VFS seek switch and all 10 in the 9P server request
switch were removed without fake success replies or disabled checks.
The strict-all report has **zero** blocking diagnostics for kernel/vfs.c
and fs/9p.c. This is NOT a whole-project certification.

The real vfs_seek now resolves SEEK_END against the canonical file size
rather than incorrectly treating it as SEEK_SET. INT64_MIN/INT64_MAX,
negative underflow, positive overflow and invalid whence are rejected
without changing the offset. A real file write/seek/readback and
adversarial bounds test is included in the integrated suite.

The remaining 242 Base-NEXS blockers and the imported Linux 9P source
goto violations in the separate NXOLMO check remain mandatory gates.
No exemption or partial pass is counted as Power12 acceptance.

## Remaining P3/P4/P5 defects (NOT solved by this cutover)

1. RegKey still holds a mutable tree with **linear sibling lookup** and
   fixed 63-character components / 511-character paths; an entirely
   NXOLMO-owned indexed name graph/namespace is not done. The measured
   2,400-sibling lookup is far too slow for the intended scale.
2. The FD table and 9P FIDs remain **singleton hosted sessions**, not
   per-NXEC-domain capability-owned handles. Dynamic capacity does not
   imply isolation or safe concurrent access. 9P FID lookup is still linear.
3. File content still uses the legacy Value string, not NXOLMO
   buffer/page leases. Binary NUL payloads are explicitly rejected;
   there is no sparse SSD-backed extent, WAL, zero-copy page transfer,
   cache-coherent DMA or true atomic multi-node transaction.
4. The old mount table still has a fixed cap and does not implement
   a Plan 9 per-domain name-space/union resolver. Real NXEC device
   contracts and full 9P2000.L semantics are not implemented. Existing
   hosted sysio and service-level VFS projections still need migration.
5. Whole-tree strict Power12, concurrent linearizability, fault
   injection, real kernel guest NXEC/OS1 tests and portability
   remain blocking. User explicitly blocked the NexsOS1 OS1 build.

Do not treat the new dynamic FD/FID arrays as an independent resource
authority: actual resource identity and revision remain only in
NXOLMO StateStore. All cross-domain transfers and hardware access
will require NXEC checks. No claim of zero-copy file payloads yet.
