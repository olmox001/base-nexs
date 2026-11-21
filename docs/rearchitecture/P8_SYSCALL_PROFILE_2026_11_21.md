# P8 — Measured real hosted syscall fuzz profile

Status: empirical hostname-specific workload, NOT complete P8, NXEC
authority qualification, kernel guest execution or 9P2000.L.
Source: actual kernel/syscall.c dispatch, wire KernelMsg encode/decode
and a real POSIX REG_IPC queue, not stub handlers.

## Evidence and reproducibility

- Seed: 2834326767407393. Actual randomized dispatches: 1,000,000 + 63 real preflight calls = 1,000,063 across 63 verb/path pairs.
- Separate 9P server calls: 250,000. All assertion failures: 0.
- Wall elapsed: 665.488 s; remaining NXREG objects: 864. Counts agree between log and CSV.
- CSV SHA256: b0d03525a5e8bcea4fb23b02fa51674f0c335411fd6997ec3bd2308c419c93c1.
- Latencies are per-real-call, including the relevant transport path,
  with host wall-clock noise and profiling overhead.
- p99 values are *upper bounds of logarithmic latency buckets*,
  NOT exact quantiles. Rejections are valid typed failures under
  hostile workloads, NOT successful operations or assertion errors.

## Full per-syscall telemetry (ranked by REG_IPC mean)

| Verb | Direct mean us | Wire mean us | REG IPC mean us | REG IPC calls | REG IPC p99 upper us | REG IPC Buddy requests/call |
|---|---:|---:|---:|---:|---:|---:|
| fstat | 1243.91 | 1468.54 | 1953.49 | 15,963 | 4194.30 | 8.50 |
| stat | 1244.87 | 1486.10 | 1928.35 | 15,749 | 4194.30 | 8.53 |
| open | 1014.73 | 1267.19 | 1728.55 | 15,631 | 4194.30 | 7.47 |
| create | 1005.74 | 1257.74 | 1705.07 | 16,144 | 4194.30 | 7.48 |
| exec | 465.02 | 694.42 | 1173.18 | 15,758 | 2097.15 | 5.00 |
| pipe | 457.93 | 691.37 | 1161.47 | 15,909 | 2097.15 | 5.00 |
| chdir | 466.45 | 676.31 | 1160.65 | 15,875 | 2097.15 | 5.00 |
| mkdir | 426.99 | 654.06 | 1117.04 | 15,815 | 2097.15 | 4.87 |
| write | 297.03 | 525.02 | 985.54 | 15,897 | 2097.15 | 5.09 |
| read | 242.41 | 471.27 | 931.63 | 15,946 | 1048.58 | 4.18 |
| fork | 225.10 | 464.38 | 920.65 | 15,651 | 1048.58 | 4.03 |
| remove | 117.55 | 340.94 | 838.16 | 15,873 | 1048.58 | 3.50 |
| mount | 117.44 | 347.17 | 810.20 | 16,015 | 1048.58 | 3.50 |
| bind | 116.94 | 343.47 | 808.23 | 15,908 | 1048.58 | 3.50 |
| exit | 9.80 | 236.47 | 708.21 | 15,798 | 1048.58 | 3.00 |
| wait | 0.63 | 225.44 | 704.89 | 16,150 | 1048.58 | 3.00 |
| dup | 0.45 | 223.55 | 701.79 | 15,720 | 1048.58 | 3.00 |
| close | 0.28 | 224.38 | 700.21 | 15,929 | 1048.58 | 3.00 |
| sleep | 7.09 | 234.08 | 697.07 | 15,837 | 1048.58 | 3.00 |
| getpid | 0.25 | 232.07 | 696.22 | 15,795 | 1048.58 | 3.00 |
| seek | 0.52 | 224.23 | 691.76 | 15,642 | 1048.58 | 3.00 |

## Interpretation and missing gates

- Separate 9P_SERVER frame test mean: 0.214 us across 250,000 requests. This is NOT a complete VFS read/write.
- The slowest hosted verbs are stat/fstat/open/create: the current
  kernel/vfs.c pathname-to-inode lookup performs repeated linear scans
  and registry Value retrieval. This source observation is a
  *candidate bottleneck*, not an instruction-level causal profile.
- REG_IPC adds serialization/copy/queue overhead; comparing means
  from differently randomized calls cannot alone identify which
  component accounts for every nanosecond.
- No process-wide, per-domain authority cache invalidation,
  shared-page lease transport or full VFS transaction is proven.
- Page counters and Buddy request/byte/failure metrics per path
  remain available in the source CSV (build/assurance) and are
  counted from production allocators. A failed request is not a
  successfully allocated page.
- Next P3/P5 acceptance requires canonical indexed NXOLMO named
  relationships replacing the linear RegKey/VFS pointer trees,
  then repeated controlled 1/2/4/8/16/32-worker measurements
  and cache/NUMA/page-fault instrumentation.

The exact input CSV/log are held in build/assurance and are excluded
from Git as runtime outputs; this report includes their provenance
and never fabricates missing metrics.
