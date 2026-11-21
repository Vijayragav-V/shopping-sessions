# Discarded experiments

Times are seconds unless marked otherwise. The section links identify the implementation and control. Several later releases reused parts of an earlier losing combination. The entry applies to the specific experiment shown.

| Experiment from [earlier native trials](BENCHMARKS.md#earlier-native-optimization-experiments) | Control → variant, 20M |
| --- | ---: |
| Extra duplicate-filter scan | 4.753 → 5.287 |
| Per-worker map of person timelines | 5.337 → 9.829 |
| Preliminary offset-only exact dedup scan | 4.183 → 4.808 |
| Block-allocated event candidates | 1.661 → 1.687 |
| Radix-sort long user timelines | 1.418 → 1.466 |
| Collect all field boundaries before row decoding | 1.373 → 1.505 |
| Duplicate-table keys alongside winner pointers | 0.911 → 0.968 |
| Snappy Parquet compression | 1.119 → 1.200 |

| [November 20 x86 trials](BENCHMARKS.md#november-20-x86-64-experiments) | Paired result |
| --- | --- |
| 24-byte event revisions with timestamp/ingest overflow arena | 3.789 → 4.489 |
| Isolated grouped clock-digit conversion in that end-to-end trial | 3.891 → 4.186. The isolated timestamp kernel subsequently won its own comparison. |
| Stream typed Parquet builder batches while summarizing | 4.174 → 4.509 |
| Single event word with overflow path on that layout | 4.464 → 4.645 |
| Combined implementation with single-pass AVX2 scanner | 3.663 → 3.746 |
| Larger bounded initial capacity, batched probes, 512/128 shard variants | 3.636 → 3.836 / 3.662; at 60M 10.008 → 10.483 (larger hint) / 10.515 (128 plus batching) |

| [Architecture](BENCHMARKS.md#native-x86-64-architecture-comparisons) and [graph/memory](BENCHMARKS.md#native-intel-graph-and-memory-trials) trials | Finding |
| --- | --- |
| Per-worker whole-input duplicate fingerprint bitmap | 2.150 → 2.929 despite cheaper exact dedup |
| Batch-wide quote grammar / lazy offsets on old layout | 2.150 → 2.171 / 2.135; neither became the release parser |
| Eight-record shard write buffer on compact layout | 1.712 → 1.718 |
| Batch AVX-512 vs row AVX2 on compact layout | 1.648 vs 1.629 |
| Defer ingest conversion and compare F1 bytes on collisions | 1.356 → 1.419 |
| Whole-program Haswell or Skylake AVX-512 target in place of dispatch | 1.956 → 1.951 / 1.999. Portable dispatch retained; old x86-64-v3 QEMU win was not a native qualification. |
| Bounded atomic global winner table | 0.787 → 1.126 arm64; 3.756 → 4.361 translated amd64 |
| Radix fingerprints with exact-key resolution; per-shard duplicate bitmap | Intel/AMD 1.527/2.012 and 1.433/2.025; displaced by fused-graph slabs (1.298/1.767) |
| Separate sampled-capacity graph; serial union | Separate graph 60M 4.508/6.201 vs fused 4.172/5.928; serial 20M 1.439/2.029 vs parallel 1.434/2.022 |
| Fused vectors at 128 / 1,024 shards | 1.375 → 1.397 / 1.395 |
| PCLMUL/BMI2 quote validation on slabs | Intel 1.298 → 1.319; both 60M medians regressed |

| [Decoder, storage](BENCHMARKS.md#experiments-against-the-099-s-release), [compiler and scheduling](BENCHMARKS.md#compiler-revision-index-and-csv-scanner-comparisons) | Finding |
| --- | --- |
| Lazy field boundaries; cache whole row layouts (1,024/4,096/16,384 entries) | Lazy 60M 3.6091 → 3.6160; caches 20M 1.1873 → 1.2193/1.2054/1.2404 |
| Group aliases during ingestion into 8/64-record blocks | 1.1873 → 3.9057/3.7324; also regressed on arm64 |
| Linked spans fed directly to bins; per-alias chains compacted or processed in place | 1.1865 → 1.1959/1.6587/1.6468 |
| Interleave both revision and raw-record slabs | 60M 3.6981 vs 3.5975 for raw/click-only interleaving on EPYC |
| Add hash tag to winner locator | Seven-pair rerun 1.1767 → 1.1883 on 6975P-C; different rank on 8370C |
| Smaller base-36 revision with separate fallback partitions | 1.1885 → 1.2156; peak 8,283 → 8,531 MiB |
| GCC Skylake tuning, GCC PGO+LTO, third PGO population | 1.1929/1.1976/1.1837 vs 1.1885; third-population result within variation of independent builds at 60M |
| Clang 16 PGO, ordinary or ThinLTO; LTO instead of GCC PGO | 1.2032/1.2366 vs 1.1885; vector trials favored PGO over LTO on Intel and AMD |
| Carry quote/newline/delimiter masks across 64-byte blocks using AVX2/AVX-512; row-wise AVX-512 | 1.1183 → 1.2116/1.2020/1.1337; all three lost at 60M |
| VBMI2 packed delimiter positions, changed short-ID loads, worker scheduling/persistent pinned team | Hardware-sensitive. VBMI2 seed 733 tied 2.5912/2.5911 on 9V45; pinning 7763 lost on 733 despite winning on 47. |

| [Timestamp/session trials](BENCHMARKS.md#timestamp-conversion-and-session-reduction) and [profile-guided release](BENCHMARKS.md#reversible-ids-and-profile-guided-compilation) | Finding |
| --- | --- |
| Sample and route frequent aliases separately; partial half-hour bins at two thresholds | 1.6876 → 1.7124 / 1.6989 / 1.7149 |
| Delay ingest-clock reading until a competing revision; order-preserving civil clocks | 1.6876 → 1.7643 / 1.6911; civil clocks also lost on both 60M seeds |
| Checked SSSE3 epoch decoder | Intel 1.6876 → 1.6974; AMD practice gain reversed in Docker at 60M |
| Accumulate unsorted sessions as 1/3/8 intervals per alias; bounded one/eight interval union | arm64 0.494 → 0.575/0.578/0.573; Intel 1.1773 → 1.1873/1.2129 |
| Direct campaign bitsets; four-byte events with indirect purchase payloads | 1.1773 → 1.2100 / 1.2016 |
| Session-index interval attribution starting at 32 clicks; chunk prefetch or sequential mmap advice on that build | 60M 3.6877 → 3.7378/3.7511/3.7361; no cold 60M gain from advice |

| [Partition writes](BENCHMARKS.md#batched-event-decoding-and-dense-campaign-reduction), [fused validation](BENCHMARKS.md#streamed-user-output-revision-columns-and-fused-event-validation), [reuse release](BENCHMARKS.md#reuse-revision-storage-for-grouped-timelines) | Finding |
| --- | --- |
| Dedicated concurrent revision consumer; write-combining raw records | Intel 1.1976 → 2.0907 / 1.3429; consumer also lost on AMD |
| Conservative first-field duplicate filter before full CSV decoding; per-alias event arenas | 1.2208 → 1.6993 / 1.3464 |
| Two field-width predictors validated against each record; collect up to one session before click sort | 1.2208 → 1.3481 / 1.3536 / 1.2382 |
| Fully concurrent atomic first-claim table; four-row timestamp validation; whole-block mask certificate | 1.115 → 2.255 / 1.147 / 1.204 |
| Delay revision writes; stage 16 input rows; alternative row representation | None beat the combined release on both Intel CPUs. Delayed writes tied the pushed practice control at 1.1145 on 6975P-C. |
