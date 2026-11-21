# P8 — Measured real hosted syscall fuzz profile

Status: empirical hostname-specific workload, NOT complete P8, NXEC
authority qualification, kernel guest execution or 9P2000.L.
Source: actual kernel/syscall.c dispatch, wire KernelMsg encode/decode
and a real POSIX REG_IPC queue, not stub handlers.

## Evidence and reproducibility

- Seed: 2834326767407393. Actual randomized dispatches: 1,000,000 + 63 real preflight calls = 1,000,063 across 63 verb/path pairs.
- Separate 9P server calls: 250,000. All assertion failures: 0.
- Wall elapsed: 425.088 s; remaining NXREG objects: 842. Counts agree between log and CSV.
- CSV SHA256: a2079a194e81bb49bbc9080a199d4ee113fb099ac660e457ebf7f8b659f3dfc6.
- Latencies are per-real-call, including the relevant transport path,
  with host wall-clock noise and profiling overhead.
- p99 values are *upper bounds of logarithmic latency buckets*,
  NOT exact quantiles. Rejections are valid typed failures under
  hostile workloads, NOT successful operations or assertion errors.

## Full per-syscall telemetry (ranked by REG_IPC mean)

| Verb | Direct mean us | Wire mean us | REG IPC mean us | REG IPC calls | REG IPC p99 upper us | REG IPC Buddy requests/call |
|---|---:|---:|---:|---:|---:|---:|
| pipe | 409.90 | 624.68 | 1059.47 | 15,909 | 1048.58 | 5.00 |
| exec | 424.82 | 632.38 | 1044.57 | 15,758 | 1048.58 | 5.00 |
| chdir | 415.38 | 618.17 | 1032.73 | 15,875 | 1048.58 | 5.00 |
| write | 252.50 | 464.71 | 875.02 | 15,897 | 2097.15 | 4.67 |
| read | 220.29 | 420.16 | 844.03 | 15,946 | 1048.58 | 4.24 |
| fork | 205.34 | 410.70 | 833.85 | 15,651 | 1048.58 | 4.03 |
| fstat | 203.78 | 408.47 | 828.72 | 15,963 | 1048.58 | 4.00 |
| stat | 203.52 | 409.23 | 824.62 | 15,749 | 1048.58 | 4.00 |
| open | 151.27 | 356.24 | 804.58 | 15,631 | 2097.15 | 3.38 |
| create | 152.00 | 357.29 | 774.53 | 16,144 | 2097.15 | 3.38 |
| mount | 105.36 | 310.11 | 733.69 | 16,015 | 1048.58 | 3.50 |
| remove | 109.33 | 306.76 | 733.18 | 15,873 | 1048.58 | 3.50 |
| mkdir | 110.05 | 312.61 | 732.29 | 15,815 | 1048.58 | 3.62 |
| bind | 105.64 | 309.37 | 728.28 | 15,908 | 1048.58 | 3.50 |
| wait | 1.14 | 203.11 | 633.42 | 16,150 | 1048.58 | 3.00 |
| exit | 9.39 | 212.35 | 631.28 | 15,798 | 1048.58 | 3.00 |
| getpid | 0.23 | 206.23 | 629.09 | 15,795 | 1048.58 | 3.00 |
| sleep | 5.98 | 213.36 | 628.34 | 15,837 | 1048.58 | 3.00 |
| dup | 1.36 | 205.35 | 625.93 | 15,720 | 1048.58 | 3.00 |
| seek | 1.40 | 206.53 | 624.88 | 15,642 | 1048.58 | 3.00 |
| close | 0.27 | 202.25 | 621.03 | 15,929 | 1048.58 | 3.00 |

## All 63 measured syscall/path combinations

| Path/verb | Calls | Rejected | Mean us | P50 upper us | P95 upper us | P99 upper us | Max us | Buddy success | Buddy frees | Buddy failures | Page success | Page frees |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| direct/open | 15,867 | 1,987 | 151.270 | 16.384 | 524.288 | 1048.576 | 9936.000 | 6,156 | 2,052 | 0 | 2 | 2 |
| direct/read | 15,918 | 12,016 | 220.292 | 131.072 | 262.144 | 524.288 | 14625.000 | 19,820 | 19,820 | 0 | 0 | 0 |
| direct/write | 16,044 | 9,403 | 252.500 | 0.512 | 1048.576 | 1048.576 | 5984.000 | 27,021 | 27,021 | 0 | 0 | 0 |
| direct/close | 16,189 | 3,686 | 0.275 | 0.001 | 0.512 | 0.512 | 39.000 | 0 | 0 | 0 | 0 | 0 |
| direct/stat | 15,802 | 2,009 | 203.523 | 131.072 | 262.144 | 262.144 | 16442.000 | 15,802 | 15,802 | 0 | 0 | 0 |
| direct/mkdir | 15,955 | 11,890 | 110.053 | 32.768 | 262.144 | 262.144 | 15328.000 | 9,916 | 7,888 | 0 | 0 | 0 |
| direct/create | 16,123 | 1,982 | 152.005 | 32.768 | 524.288 | 1048.576 | 13525.000 | 6,198 | 2,066 | 0 | 2 | 2 |
| direct/remove | 15,778 | 9,843 | 109.334 | 16.384 | 262.144 | 262.144 | 37325.000 | 7,829 | 17,763 | 0 | 0 | 0 |
| direct/bind | 15,801 | 7,883 | 105.638 | 16.384 | 262.144 | 262.144 | 5148.000 | 7,885 | 7,883 | 0 | 0 | 0 |
| direct/mount | 15,804 | 15,799 | 105.360 | 16.384 | 262.144 | 262.144 | 8281.000 | 7,915 | 7,869 | 0 | 0 | 0 |
| direct/fork | 15,985 | 15,942 | 205.341 | 131.072 | 262.144 | 262.144 | 50481.000 | 16,501 | 15,942 | 0 | 3 | 3 |
| direct/exec | 15,873 | 15,873 | 424.824 | 262.144 | 524.288 | 524.288 | 66167.000 | 31,746 | 31,746 | 0 | 0 | 0 |
| direct/wait | 16,128 | 0 | 1.145 | 0.512 | 0.512 | 0.512 | 9255.000 | 0 | 0 | 0 | 0 | 0 |
| direct/exit | 15,940 | 0 | 9.394 | 8.192 | 8.192 | 16.384 | 1382.000 | 0 | 0 | 0 | 0 | 0 |
| direct/chdir | 15,845 | 0 | 415.377 | 262.144 | 524.288 | 524.288 | 48549.000 | 31,691 | 15,844 | 0 | 0 | 0 |
| direct/getpid | 15,892 | 0 | 0.235 | 0.001 | 0.512 | 0.512 | 12.000 | 0 | 0 | 0 | 0 | 0 |
| direct/sleep | 15,876 | 0 | 5.983 | 4.096 | 8.192 | 16.384 | 71.000 | 1 | 0 | 0 | 0 | 0 |
| direct/pipe | 16,036 | 0 | 409.900 | 262.144 | 524.288 | 524.288 | 6095.000 | 32,075 | 32,072 | 0 | 0 | 0 |
| direct/dup | 15,853 | 5,012 | 1.362 | 0.512 | 2.048 | 4.096 | 59.000 | 0 | 0 | 0 | 0 | 0 |
| direct/seek | 15,744 | 9,373 | 1.402 | 0.512 | 2.048 | 4.096 | 31.000 | 0 | 0 | 0 | 0 | 0 |
| direct/fstat | 15,769 | 1,973 | 203.778 | 131.072 | 262.144 | 262.144 | 3815.000 | 15,769 | 15,769 | 0 | 0 | 0 |
| wire/open | 15,654 | 1,957 | 356.237 | 262.144 | 524.288 | 1048.576 | 32242.000 | 21,402 | 17,570 | 0 | 0 | 0 |
| wire/read | 15,937 | 12,039 | 420.161 | 262.144 | 524.288 | 524.288 | 18969.000 | 35,772 | 35,772 | 0 | 0 | 0 |
| wire/write | 15,824 | 9,290 | 464.705 | 262.144 | 1048.576 | 1048.576 | 55190.000 | 42,397 | 42,397 | 0 | 0 | 0 |
| wire/close | 15,736 | 3,543 | 202.245 | 131.072 | 262.144 | 262.144 | 6317.000 | 15,736 | 15,736 | 0 | 0 | 0 |
| wire/stat | 16,017 | 1,914 | 409.226 | 262.144 | 524.288 | 524.288 | 14079.000 | 32,034 | 32,034 | 0 | 0 | 0 |
| wire/mkdir | 15,953 | 12,000 | 312.606 | 262.144 | 524.288 | 524.288 | 10162.000 | 25,907 | 23,959 | 0 | 0 | 0 |
| wire/create | 15,846 | 1,955 | 357.290 | 262.144 | 524.288 | 1048.576 | 29801.000 | 21,822 | 17,838 | 0 | 2 | 2 |
| wire/remove | 16,062 | 10,012 | 306.755 | 262.144 | 524.288 | 524.288 | 7439.000 | 24,014 | 34,094 | 0 | 0 | 0 |
| wire/bind | 15,994 | 8,026 | 309.365 | 262.144 | 524.288 | 524.288 | 10759.000 | 24,020 | 24,020 | 0 | 0 | 0 |
| wire/mount | 15,701 | 15,692 | 310.111 | 262.144 | 524.288 | 524.288 | 22290.000 | 23,692 | 23,611 | 0 | 0 | 0 |
| wire/fork | 15,734 | 15,691 | 410.704 | 262.144 | 524.288 | 524.288 | 58500.000 | 31,984 | 31,425 | 0 | 0 | 0 |
| wire/exec | 15,640 | 15,640 | 632.379 | 524.288 | 1048.576 | 1048.576 | 33678.000 | 46,920 | 46,920 | 0 | 0 | 0 |
| wire/wait | 15,829 | 0 | 203.106 | 131.072 | 262.144 | 262.144 | 13211.000 | 15,829 | 15,829 | 0 | 0 | 0 |
| wire/exit | 15,708 | 0 | 212.350 | 131.072 | 262.144 | 262.144 | 33785.000 | 15,708 | 15,708 | 0 | 0 | 0 |
| wire/chdir | 15,935 | 0 | 618.167 | 524.288 | 1048.576 | 1048.576 | 73405.000 | 47,806 | 31,869 | 0 | 0 | 0 |
| wire/getpid | 15,805 | 0 | 206.227 | 131.072 | 262.144 | 262.144 | 17570.000 | 15,805 | 15,805 | 0 | 0 | 0 |
| wire/sleep | 15,898 | 0 | 213.364 | 131.072 | 262.144 | 262.144 | 15483.000 | 15,899 | 15,898 | 0 | 0 | 0 |
| wire/pipe | 15,875 | 0 | 624.677 | 524.288 | 1048.576 | 1048.576 | 96581.000 | 47,628 | 47,625 | 0 | 0 | 0 |
| wire/dup | 15,781 | 5,010 | 205.354 | 131.072 | 262.144 | 262.144 | 19453.000 | 15,781 | 15,781 | 0 | 0 | 0 |
| wire/seek | 16,122 | 9,475 | 206.526 | 131.072 | 262.144 | 262.144 | 36089.000 | 16,122 | 16,122 | 0 | 0 | 0 |
| wire/fstat | 15,785 | 1,973 | 408.466 | 262.144 | 524.288 | 524.288 | 9664.000 | 31,570 | 31,570 | 0 | 0 | 0 |
| REG_IPC/open | 15,631 | 1,970 | 804.580 | 524.288 | 1048.576 | 2097.152 | 75193.000 | 52,776 | 48,854 | 0 | 2 | 2 |
| REG_IPC/read | 15,946 | 12,081 | 844.033 | 524.288 | 1048.576 | 1048.576 | 42013.000 | 67,649 | 67,649 | 0 | 0 | 0 |
| REG_IPC/write | 15,897 | 9,384 | 875.017 | 524.288 | 1048.576 | 2097.152 | 39693.000 | 74,206 | 74,206 | 0 | 0 | 0 |
| REG_IPC/close | 15,929 | 3,721 | 621.030 | 524.288 | 1048.576 | 1048.576 | 8023.000 | 47,787 | 47,787 | 0 | 0 | 0 |
| REG_IPC/stat | 15,749 | 1,952 | 824.620 | 524.288 | 1048.576 | 1048.576 | 41977.000 | 62,996 | 62,996 | 0 | 0 | 0 |
| REG_IPC/mkdir | 15,815 | 11,876 | 732.287 | 524.288 | 1048.576 | 1048.576 | 21098.000 | 57,312 | 55,357 | 0 | 0 | 0 |
| REG_IPC/create | 16,144 | 2,027 | 774.527 | 524.288 | 1048.576 | 2097.152 | 62126.000 | 54,546 | 50,470 | 0 | 2 | 2 |
| REG_IPC/remove | 15,873 | 9,910 | 733.178 | 524.288 | 1048.576 | 1048.576 | 44310.000 | 55,521 | 65,472 | 0 | 0 | 0 |
| REG_IPC/bind | 15,908 | 7,961 | 728.277 | 524.288 | 1048.576 | 1048.576 | 26888.000 | 55,691 | 55,685 | 0 | 0 | 0 |
| REG_IPC/mount | 16,015 | 16,013 | 733.695 | 524.288 | 1048.576 | 1048.576 | 67242.000 | 56,022 | 56,004 | 0 | 0 | 0 |
| REG_IPC/fork | 15,651 | 15,609 | 833.850 | 524.288 | 1048.576 | 1048.576 | 80773.000 | 63,108 | 62,562 | 0 | 3 | 3 |
| REG_IPC/exec | 15,758 | 15,758 | 1044.573 | 524.288 | 1048.576 | 1048.576 | 8983.000 | 78,790 | 78,790 | 0 | 0 | 0 |
| REG_IPC/wait | 16,150 | 0 | 633.420 | 524.288 | 1048.576 | 1048.576 | 110984.000 | 48,450 | 48,450 | 0 | 0 | 0 |
| REG_IPC/exit | 15,798 | 0 | 631.279 | 524.288 | 1048.576 | 1048.576 | 9031.000 | 47,394 | 47,394 | 0 | 0 | 0 |
| REG_IPC/chdir | 15,875 | 0 | 1032.727 | 524.288 | 1048.576 | 1048.576 | 50927.000 | 79,375 | 63,500 | 0 | 0 | 0 |
| REG_IPC/getpid | 15,795 | 0 | 629.085 | 524.288 | 1048.576 | 1048.576 | 65738.000 | 47,385 | 47,385 | 0 | 0 | 0 |
| REG_IPC/sleep | 15,837 | 0 | 628.336 | 524.288 | 1048.576 | 1048.576 | 31968.000 | 47,511 | 47,511 | 0 | 0 | 0 |
| REG_IPC/pipe | 15,909 | 0 | 1059.473 | 524.288 | 1048.576 | 1048.576 | 75353.000 | 79,545 | 79,545 | 0 | 0 | 0 |
| REG_IPC/dup | 15,720 | 4,991 | 625.931 | 524.288 | 1048.576 | 1048.576 | 32668.000 | 47,160 | 47,160 | 0 | 3 | 3 |
| REG_IPC/seek | 15,642 | 9,205 | 624.878 | 524.288 | 1048.576 | 1048.576 | 16637.000 | 46,926 | 46,926 | 0 | 0 | 0 |
| REG_IPC/fstat | 15,963 | 2,005 | 828.725 | 524.288 | 1048.576 | 1048.576 | 69948.000 | 63,852 | 63,852 | 0 | 0 | 0 |

- Counted Buddy successes: 2,038,175; counted frees: 1,988,675; allocation failures: 0.
- Counted page successes: 19; page frees: 19.
- These deltas measure the instrumented syscall windows ONLY.
  They are NOT a full-process leak proof: startup, retained objects,
  exit reclamation and provider-internal caches are outside windows.


## Interpretation and missing gates

- Separate 9P_SERVER frame test mean: 0.204 us across 250,000 requests. This is NOT a complete VFS read/write.
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

## Paired workload observations (same seed and 1,000,000 random dispatches)

| Operation | Legacy mean µs | Single-object mean µs | Relative ratio | Old Buddy req/call | New Buddy req/call |
|---|---:|---:|---:|---:|---:|
| direct/stat | 1244.87 | 203.52 | 6.12× | 5.49 | 1.00 |
| wire/stat | 1486.10 | 409.23 | 3.63× | 6.52 | 2.00 |
| REG_IPC/stat | 1928.35 | 824.62 | 2.34× | 8.53 | 4.00 |
| direct/fstat | 1243.91 | 203.78 | 6.10× | 5.50 | 1.00 |
| REG_IPC/fstat | 1953.49 | 828.72 | 2.36× | 8.50 | 4.00 |
| direct/open | 1014.73 | 151.27 | 6.71× | 4.50 | 0.39 |
| REG_IPC/open | 1728.55 | 804.58 | 2.15× | 7.47 | 3.38 |
| direct/create | 1005.74 | 152.00 | 6.62× | 4.46 | 0.38 |
| REG_IPC/create | 1705.07 | 774.53 | 2.20× | 7.48 | 3.38 |

**Comparability check:** exact same 63 verb/path names AND identical
per-verb dispatch counts, seed and one million random cases.
These are two independent runs (not controlled time-adjacent
repetitions); the newer run was also sampled by the macOS CPU profiler.
Neither causal attribution nor future hardware performance is guaranteed.
The collected measurements are HOSTED and do not prove NXEC
capabilities, namespace isolation, zero-copy files or NexsOS1 OS1 boot.
