# Reproducing the Shopping Sessions measurements

The published generator and checker from the challenge distribution are used unchanged. In this workspace they are under `generator/` and `starter-pyspark/scripts/`, with the seed-1 answers under `dev-answer/`. The release executable loads Arrow and Parquet C++ libraries. Python is used for building and checking.

```sh
python3 -m venv .bench/env
.bench/env/bin/pip install pyarrow==21.0.0 polars==2.1.0
PYTHON=.bench/env/bin/python sh build.sh
generate --seed 1 --events 1000000 --clicks 250000 --output .bench/dev
generate --seed 23 --events 20000000 --clicks 5000000 --output .bench/practice23
generate --seed 47 --events 60000000 --clicks 15000000 --output .bench/final47
.bench/env/bin/python verify.py build/shopping .bench/dev
NCPU=8 python3 bench.py .bench/practice23 build/shopping --runs 7
```

Pass two executable paths to `bench.py` to alternate their order. Timing starts before launching the process and ends after it exits. Each invocation gets a fresh output directory. Cleanup and output comparison run outside the timer. For individual runs, use an empty output directory.

Use `--warmup 1` to run each binary once before collecting measurements. With three or more binaries the order rotates. For Linux, `IMAGE=shopping-cpp:final sh bench-container.sh INPUT FRESH_OUTPUT` pins CPUs 0–7, caps the container at eight CPUs and 24 GB, disables network access and uses a read-only root. It times a fresh executable process for each run inside one container and keeps each output for inspection.

Set `PROFILE=1` for elapsed times at phase boundaries. Build with `DIAGNOSTICS=1 PYTHON=.bench/env/bin/python sh build.sh` and run `PROFILE=1 build/shopping-profile INPUT OUTPUT` for worker-time breakdowns. Nested measurements overlap. CSV structural decoding samples chunks and subtracts the field callback time. Allocation timing samples one in 128 ordinary C++ allocations and excludes deallocation, aligned allocations, and Arrow's separate memory pool.

## Paired experiments

These are native macOS arm64 measurements on an 18 GB M3 Pro with eight application workers. Each row compares two executables in the same experiment on seed 23. Runs use the filesystem cache. Thermal state, page residency and other activity varied across experiments, so compare medians **within** a row. Every candidate below passed the 36 fixtures and both published dev tables.

| Change | Before median | Candidate median | Runs per binary |
| --- | ---: | ---: | ---: |
| Open-addressed global event deduplication | 5.840 s | 4.871 s | 3 |
| Extra duplicate-filter scan (discarded) | 4.753 s | 5.287 s | 3 |
| SIMD delimiter and quote scanning | 7.128 s | 6.085 s | 3 |
| Per-worker map of person timelines (discarded) | 5.337 s | 9.829 s | 3 |
| Preliminary offset-only exact dedup scan (discarded) | 4.183 s | 4.808 s | 3 |
| Losslessly packed IDs and flat alias lookup | 4.141 s | 1.916 s | 3 |
| Direct campaign-day aggregation with sparse fallback | 2.244 s | 1.854 s | 3 |
| Cache civil dates and specialize guaranteed F1 ingest parsing | 1.845 s | 1.695 s | 5 |
| Count and scatter each person's timelines into contiguous spans | 1.607 s | 1.514 s | 5 |
| Block-allocate event candidates (discarded) | 1.661 s | 1.687 s | 5 |
| Bounded initial reserve for event candidates | 1.526 s | 1.453 s | 5 |
| Radix-sort large user timelines (discarded) | 1.418 s | 1.466 s | 5 |
| Build global linked identities concurrently with event parsing | 1.589 s | 1.275 s | 5 |
| Reuse campaign activity and category spend storage | 1.545 s | 1.367 s | 5 |
| Collect all field boundaries before decoding a row (discarded) | 1.373 s | 1.505 s | 5 |
| Reserve numeric Parquet builder capacity before appending | 1.685 s | 1.459 s | 5 |
| Store duplicate-table keys alongside winner pointers (discarded) | 0.911 s | 0.968 s | 7 |
| Snappy-compress Parquet output (discarded) | 1.119 s | 1.200 s | 5 |

The direct comparison of link-table preallocation and bytewise inline name comparison was essentially tied on seed 23 (1.515 vs 1.531 s). On seed 47 it improved all three paired 60M runs, with medians of 14.573 and 13.065 s. A six-run rotated comparison after one warm-up per binary found medians of 1.017, 0.934 and 0.901 s for 32, 128 and 512 shards, respectively.

Docker Desktop on Apple silicon gave a different result on the same 20M input. With eight pinned vCPUs, a 7 GiB cgroup limit and the Linux/arm64 image, direct host-mounted input took 5.869, 8.227 and 6.153 s. Copying identical input bytes to Linux tmpfs **before** measurement took 2.519, 2.398 and 2.086 s for the executable, including Parquet output to the host mount. Both configurations matched the baseline. The production executable always reads its supplied input directly. These machines and mounts differ from the Intel competition scorer.

## Final verification

* The native executable and the `linux/amd64` submission image passed all 36 published fixtures. Both matched all 117,038 user-day rows and 27,400 campaign-day rows of the independent published dev answer. The amd64 image ran under Docker Desktop using the supplied scorer script with a 7 GiB limit.
* The final 20M output matched the original C++ baseline at 2,226,509 user-day and 28,014 campaign-day rows. The 60M output matched the earlier string-key implementation at 6,684,549 and 28,014 rows. Both diffs reported zero differences and zero duplicate keys. The generated large datasets have no published independent answer here.
* In an alternating five-run native comparison, the original baseline median was 5.865 s and the retained binary median was 2.156 s. In the separate warm shard experiment above the retained binary achieved 0.901 s. Cross-run variation was substantial on this host.
* The final standalone warm-cache series used two warm-ups followed by seven timed processes: 1.313, 1.084, 0.914, 1.074, 0.874, 0.815 and 0.891 s (median **0.914 s**). The official practice input and Intel scorer were unavailable.
* Three final native runs on 60M events and 15M clicks took 17.602, 17.309 and 15.330 s. Peak process RSS from `wait4` was 4.603, 4.723 and 5.216 GiB. These are resident-process measurements on a memory-pressured Mac, not a Linux cgroup high-water mark. The seed-47 CSVs were removed after validation to reclaim space. Use the generator command above to recreate them.

On a separate instrumented warm 20M run, event CSV decoding, validation and sharding took 0.294 s elapsed, including the concurrent 0.198 s identity build. Global deduplication and person routing took 0.155 s, clicks took 0.107 s, grouped timeline processing took 0.212 s, and the final merge and Parquet writes took 0.063 s. The jobs overlap across workers. The diagnostic totals were 0.432 worker-s grouping, 0.477 sorting, 0.075 scanning session boundaries and purchases, 0.088 updating the attribution window, and 0.263 aggregating. Structural CSV scanning accounted for 0.0453 sampled worker-s over 62.4 MB of 1.918 GB. The ordinary C++ allocator handled about 926,000 allocations and 5.51 GB of cumulative requested bytes, with an estimated 0.134 worker-s in malloc. Process launch and teardown are included in the release benchmarks.

## Correctness invariants

* Event ID determines the deduplication shard, including revisions that change user. A CSV-valid but semantically invalid revision participates in earliest-ingest selection. The winning revision must pass validation before entering a person's timeline.
* Compact keys preserve byte identity. Long IDs, unusual prefixes and strings that cannot be represented inline take an exact string-view path. Hash collisions always compare keys. A linked component's display name includes IDs that never appear in events or clicks.
* Repeated identical clicks can remain in the sorted timeline. They preserve the latest timestamp and smallest click ID, and campaign counts contribute one assist while positive. Each person's session and click windows advance monotonically.
* The Parquet diff checks the schema, values, nulls and duplicate keys. The extra randomized parser test compares accepted fields with a scalar decoder and checks compact keys against byte strings. Run it with `c++ -std=c++20 -O2 tests/input.cpp -o build/input-check && build/input-check`.
