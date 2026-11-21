# P8 — Measured Source/Resource Throughput (2026-11-21)

Status: partial empirical baseline, **NOT** a completed P8 gate or full VFS benchmark.

## Correctness gates

- Product-linked NXOLMO authority, hardware resource, buffer, capability
  adapter and 9P frame modules. The six original upstream unit/concurrency
  regression suites pass against product source; they do not substitute for
  per-function P12 proof or whole-tree sanitizers.
- New `tests/test_p9_request_validation.c`: 9P2000 frame, fixed/string
  bodies, truncated inputs, inconsistent write count, invalid opcode
  and malformed trailing fields. Legacy server now decodes and admits through
  this canonical provider, rather than reading arbitrary frames unchecked.
- `tests/perf/billion_p9.c`: 8 independent worker threads, exactly
  1,000,000,000 calls to the **actual product** `nx_9p_frame_decode`.
  999,999,000 frames also pass `nx_9p_request_validate`; 1,000
  intentionally truncated frames are rejected. Every valid response checks
  tag, body, consumed bytes. PASS.
- `tests/perf/billion_masked_buffer.c`: uses **real**
  `nx_capability_issue` / `nx_buffer_capability_authorize`, NXOLMO
  StateStore, buffer manager, authorized writable lease, bit-level direct
  mask updates and **one** NXOLMO commit. Token expiry is checked
  negatively; every byte independently checked after commit, verified by
  `nx_buffer_verify`; all lease/arena reclamation checked. PASS.
  A deterministic test key and explicit trusted test resolver exercise
  production crypto/authority code, not production kernel trust issuance.

## Observed real execution (macOS host, optimized C product implementation)

| Workload | Operations | Workers | Elapsed | Rate | Result |
|---|---:|---:|---:|---:|---|
| 9P frame requests, full input validation | 1,000,000,000 | 8 | 18.067228 s | 55.349 million requests/s | PASS |
| 9P frame scaling sample | 50,000,000 | 1 | 0.790810 s | 63.226 million requests/s | PASS |
| 9P frame scaling sample | 50,000,000 | 2 | 1.449845 s | 34.486 million requests/s | PASS |
| 9P frame scaling sample | 50,000,000 | 4 | 1.542253 s | 32.420 million requests/s | PASS |
| 9P frame scaling sample | 50,000,000 | 8 | 0.874430 s | 57.180 million requests/s | PASS |
| Zero-copy 1 MiB bit-mask updates under signed NXOLMO write lease | 1,000,000,000 | 1 | 1.791802 s | 558.097 million updates/s | PASS |
| NXOLMO buffer SHA/CRC + revision commit, entire 1 MiB | 1 commit | 1 | 0.011776 s | one committed batch | PASS |

The 1/2/4/8-worker samples are **not stable scaling proof**. Host scheduling,
power management and workload variation may dominate; repeat with pinned/controlled
conditions and report confidence intervals. The bitmask loop does not test a
billion separate authority checks or a billion commits. The 9P workload is
not a networked filesystem round-trip and not a throughput result for the
legacy FID/VFS server.

## Run and artifact commands

```sh
make test-nxolmo-resource-conformance
make test-p9-request-conformance
make perf-billion-p9 NX_P9_BENCH_OPERATIONS=1000000000 NX_P9_BENCH_WORKERS=8
make perf-billion-buffer NX_BUFFER_BENCH_OPERATIONS=1000000000 NX_BUFFER_BENCH_BYTES=1048576
```

Logs under `build/assurance/` on the verification host:
`p9_perf_billion.log`, `p9_worker_scaling_50m.log`,
`nxolmo_buffer_perf_billion.log` and baseline test logs. Logs are
build outputs, not source-control artifacts.

## Real syscall and allocation fuzzing\n\nHosted actual kernel dispatcher, message codec, REG IPC POSIX pipe, NXOLMO\nregistry, 9P real server and VFS integration test are covered in\nP8_REAL_SYSCALL_FUZZ_2026_11_21.md. This is not guest or billion-commit\nqualification, and source/architecture gaps remain explicit.\n\n## Whole hosted VFS/9P full-state measurements

- 1,000,000 actual 9P writes, each with a new NXOLMO StateStore
  revision and a checked canonical object reference, plus 1,000,000
  actual 9P reads whose bytes match independent expected contents.
  Full wall-clock throughput including random inputs and StateStore
  checking: **167,236.9 successful full operations/s** on the
  measured hosted Mac run (not 1e9 commits).
- 100,000 additional randomized **semantic denial** wire requests
  across eight 9P verb types, checked as denied and verified to leave
  the canonical file reference, revision and bytes unchanged.
- Full product source compiled with ASan+UBSan passed 1,000
  commits/readback pairs plus 10,000 denial mutations (Mac host).
  This is NOT a guest, thread-safety or power-loss proof.
- 256 actual .nx programs generated and executed against interpreter:
  198 successful valid-program oracle checks and 58 hostile
  containment cases, zero errors in this batch.
  Interpreter per-program startup is included in language fuzz latency.

Artifacts: build/assurance/p9_full_1m_commits.log,
p9_hostile_100k_e2e.log, p9_full_sanitizers.log and
nx_language_fuzz_full/report.json.

## Canonical named-file backend (new source, separate from earlier baseline)

The previously benchmarked linear inode table has been replaced in
kernel/vfs.c by one NXOLMO-owned object per file under /vfs/files.
No /vfs/inode/N table is created. The 9P Qid version updates with the
file's own StateStore revision on each accepted write. The FD and
9P FID session slots both grow via page providers, with generation
checks after delete/recreate.

An independent source-backed suite creates 2,400 files, holds 192
VFS FDs plus 192 9P FIDs and checks 8,192 real random stats,
all reads and StateStore validity. A preliminary single-hosted
run of 100,000 actual 9P write commits + 100,000 readbacks
measured 115,652.2 full operations/s, with mean write 12,608.0 ns
and read 3,998.7 ns. These values are one sample subject to host
noise and are NOT comparable as a controlled A/B against the older
165k-operations/s profile.

The RegKey sibling scan and 9P FID lookup are still linear, so
this cutover does not demonstrate constant-time namespace scaling,
full zero-copy payloads, isolated NXEC sessions or 9P2000.L.

## One million new-backend full file commits (requalified)

After the one-object file migration, the actual product passed another
1,000,000 TWRITE calls that increment the SAME StateStore file
revision, with 1,000,000 independent 9P TREAD byte comparisons,
plus the 2,400-file and 192+192 FD/FID scale/stale-generation tests.
The observed full wall-clock throughput was **135,085.1 ops/s**
(mean internal write 10,632.3 ns, read 3,484.3 ns) on the hosted Mac.
Do not infer 1e9 commits or TFLOPS, or assume this is an A/B speedup.

## Missing hardware and correctness gates

- NXEC HAL contract and trusted authority scope/epoch revocation,
  physical/virtual bit-range and MMIO masking, IOMMU, NUMA/caches.
- Per-instance NXOLMO object namespace; full indexed NXREG name lookup,
  MVCC transactions; no global mutable RegKey/FID/FD hot paths.
- Full 9P2000.L operation semantics/transport, mount namespaces, ext4, WAL,
  page-lease transfer and process-kill recovery.
- Measured true end-to-end operations under saturating 1/2/4/8/16/32
  threads, contention, p50/p95/p99 and throughput stability; cross-machine
  hosts and both guest architectures.
- Actual numeric floating-point kernel performance with well-defined FLOP
  count, compiler flags, ISA/SIMD settings and hardware peak. **TFLOPS**
  cannot be inferred from 9P decode, capability checks or bitmask updates.
- Whole-code strict Power12, two asserted tests per production function,
  TSan/ASan/UBSan, fault injection, linearizability and real NexsOS1
  integration. OS1 build remains blocked by explicit user instruction.

No invented zero-failure guarantee or "fastest in history" statement is made.
Resource exhaustion, revoked authority and contention must fail explicitly
and must not cause memory corruption or false success.
