# Reproducing the Shopping Sessions measurements

## Official score history

| Git revision | Hidden 20M Intel Linux time | Production changes |
| --- | ---: | --- |
| `61e9ba65eafa2dc9a002c03aa2bbf4135931d448` | **1.80 s** | Original scored release |
| `1bcbf0cd1a2754fb8812fd91e2ef5f4237dc928e` | **1.82 s** | Only `.gitignore` changed |
| `8b2f568600d0fb5cc9bc066dc13ce609ed5d0e7b` | **1.83 s** | First 512-shard release |

The entrant supplied all three scores. Entries `BB-065C223B` and `BB-E8D93ABE` listed 41 ms and 105 ms of queue wait separately. Relative to `61e9ba6`, the 512-shard release decodes each field as its delimiter is encountered, raises the event and person shard count from 128 to 512, reserves identity storage from input size, and compares inline canonical IDs bytewise without a separate display-name vector. It also reserves Arrow builder capacity and uses unchecked numeric appends within that reservation. Its compiler flags, Dockerfile, global deduplication rules, session boundaries and attribution algorithm match the official baseline. Its official run was 0.03 s slower. Two submissions of the identical baseline pipeline differed by 0.02 s.

## Submission candidate

The candidate adds a runtime-dispatched AVX2/BMI/POPCNT CSV scanner, locates complete rows on the worker reading each chunk, and decodes common timestamp formats with fixed-width integer operations. Non-AVX2 processors retain the existing parser. The union-find pass assigns each linked person a permanent position in its shard. Each worker caches exact raw-ID routes, and catalog lookups use direct slots for canonical decimal identifiers with a full byte-string fallback. Parquet uses delta binary packing for dates and integers and omits optional column statistics. The executable reads the mounted inputs directly and writes only the two required output tables.

### Timed release binary

The actual Docker release executable and the `61e9ba6` baseline ran in alternating order inside the same Linux tmpfs environment. Both used the same seed-23 input, eight pinned virtual CPUs and fresh outputs, with one warm-up each. These timings cover process startup through exit under QEMU on Apple silicon. The candidate finished first in all five pairs. Its median was 14.1% lower and median process CPU time fell from 32.95 to 28.22 s.

| 20M translated amd64 | Five timed runs (s) | Median | Maximum process RSS | Combined Parquet size |
| --- | --- | ---: | ---: | ---: |
| `61e9ba6` | 4.451, 4.427, 4.466, 5.447, 5.192 | 4.466 s | 3,394 MiB | 97.3 MB |
| Candidate | 3.511, 3.836, 3.938, 3.790, 4.567 | **3.836 s** | 3,371 MiB | 43.0 MB |

On the native M3 Pro, an independent five-pair series used the same eight workers, a sequential preload and page-residency sampling. All timed processes started with effectively 100% input residency. The scored baseline took 1.025 s median (0.946–1.574 s). The candidate took **0.880 s median** (0.776–0.917 s), with a maximum peak RSS of 4,210 MiB. Both executables ran in a fresh process each time. These figures measure arm64 execution.

| Median elapsed phase on the same runs | `61e9ba6` | Candidate |
| --- | ---: | ---: |
| Event CSV, validation and partition | 2.247 s | 1.836 s |
| Identity resolution, concurrent with event input | 0.875 s | 0.788 s |
| Global event deduplication and routing | 0.602 s | 0.486 s |
| Click CSV and routing | 0.471 s | 0.392 s |
| Grouping, sorting, sessions, attribution and aggregation | 0.654 s | 0.547 s |
| Merge and Parquet output | 0.479 s | 0.374 s |

Event input is the largest elapsed phase. The candidate processed 1.65 GB of event CSV at roughly 0.90 GB/s in this phase, including field validation, partition writes and the concurrent link build. A separate diagnostic run measured 0.467 worker-seconds of structural CSV decoding over a 64.1 MB sample of 1.918 GB. The event partition held 860 MB of live revisions in 1.410 GB of vector capacity. Deduplication took 0.755 worker-seconds and routing, copying and releasing event bins took 3.554. Grouping took 1.138, sorting 1.842, session boundaries 0.780, attribution 0.816, aggregation 1.659 and user Parquet encoding and writes 2.542 worker-seconds. Sampled allocation time was 0.783 worker-seconds over 538,439 ordinary C++ allocations and 5.55 GB of cumulative requests. These diagnostic timings include instrumentation and overlap across threads.

The event workers handled 233–435 jobs each while their active times spanned only 3.091–3.094 s. The timeline workers handled 57–72 shards each and finished within 14 ms of one another. The largest person's timelines had 387,486 events and 72,005 clicks. In this run CSV decoding and moving surviving events consumed more time than waiting for stragglers.

### Release-image correctness and memory

Built with `docker buildx build --platform linux/amd64 --load`. The tested `/app/shopping` SHA-256 is `3d13f94c1963d5fe98148eab759ca7663f5b7de58f3ff6627dec8807970878c9`. All 36 published fixtures, the additional catalog/long-line case and both published 1M tables passed using the supplied Docker runner. The randomized decoder and timestamp tests also passed under Linux UBSan with AVX2 enabled and with a pre-AVX2 CPU model. The latter separately matched the 1M reference tables through the fallback parser.

| Input | Reference | Exact user-day rows | Exact campaign-day rows | Host-bind wall | Peak process RSS | Peak cgroup memory |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1M events, 250k clicks | Published answers | 117,038 | 27,400 | — | — | — |
| 20M events, 5M clicks | Previously validated output | 2,226,509 | 28,014 | 10.654 s | 3,361.1 MiB | 3,366.4 MiB |
| 60M events, 15M clicks | Previously validated output | 6,684,549 | 28,014 | 40.537 s | 7,134.6 MiB | 7,168.0 MiB |

Both larger comparisons checked schema, every value and duplicate keys for both tables. All keys were unique. The image read the Mac-host-mounted CSVs with eight pinned virtual CPUs, a read-only root, UID 1000, networking disabled and a 7 GiB cgroup with swap disabled. At 60M the cgroup reached that limit and the executable still completed. Event input took 24.59 s in that run and 7.48 s at 20M. The two host-bind runs incurred 13,089 and 4,230 major faults respectively. The five paired tmpfs runs above had two major faults each. The official scorer has 24 GB and runs natively on Intel. Its result for this candidate is pending.

## Benchmark conditions

The 0.914 s native median below came from seven standalone processes after two warm-ups. The 2.156 s median came from five alternating rounds against the original string-key implementation at `9068330`, after one warm-up per executable. Both experiments used seed 23, eight application workers and fresh Parquet output. The timer included process launch, output close and process teardown. The harness created and removed output directories outside the timer. The older experiment did not record page residency, faults, clock frequency or competing load, so it cannot apportion the difference between those factors.

A repeat on the same M3 Pro recorded those missing page counters. The first 512-shard process started with 0% CSV residency, took 5.412 s and incurred 117,133 major faults. With 100% starting residency the five standalone times were 0.832, 0.960, 0.905, 0.865 and 0.796 s (median 0.865 s). Alternating against `9068330` gave 1.607, 1.043, 1.187 and 0.860 s for the same 512-shard binary (median 1.115 s), again with 100% starting residency. The original string-key executable took a median 5.598 s. Ordering and host conditions affect the result even when the input starts resident. The repeat used byte-verified transparent APFS compression on the generated practice input.

This command records resource counters for two executables in alternating order.

```sh
python3 perf/measure.py .bench/practice23 path/to/before path/to/after \
  --runs 5 --warmup 1 --profile --residency --log /tmp/shopping-comparison.jsonl
```

Each JSON line records wall, user and system time, peak process RSS, faults, block I/O, context switches and phase timings. The optional `--preload` reads the input before each process. The optional `--residency` samples mapped CSV pages with `mincore` before and after each process. Neither operation falls inside the timed interval.

### November 20 x86-64 experiments

Docker Desktop ran GCC 12 amd64 binaries through QEMU on the M3 Pro. The runner used Linux CPU affinity 0–7, an eight-CPU quota, a 7 GiB cgroup and disabled swap. The same 1.918 GB seed-23 input was staged in Linux tmpfs before timing. Each process wrote fresh Parquet in tmpfs. Each experiment rotated executable order after one warm-up per binary. The official machine has Intel cores and 24 GB. The measurements below establish local comparisons only.

| Change or comparison | Control median | Candidate median | Timed runs per binary |
| --- | ---: | ---: | ---: |
| Official baseline vs. the 512-shard release | 5.244 s | 5.138 s | 4 |
| Runtime-dispatched 32-byte AVX2 delimiter and quote indexing vs. the 512-shard release | 4.985 s | 3.789 s | 3 |
| Pack timestamps and ingest into 24-byte event revisions, with an overflow arena | 3.789 s | 4.489 s | 3 |
| Assign linked components dense positions in their person shard | 3.962 s | 3.852 s | 5 |
| Decode groups of ASCII clock digits using integer operations | 3.891 s | 4.186 s | 5 |
| Divide files by byte count and locate complete rows on the worker that reads them | 4.186 s | 3.546 s | 5 |
| Stream typed Parquet column batches while summarizing people | 4.174 s | 4.509 s | 5 |
| Store the timestamp, category and purchase price in one event word with an exact overflow path | 4.464 s | 4.645 s | 5 |
| Compile the entire executable for x86-64-v3 | 4.464 s | 4.075 s | 5 |

The 61-to-512 comparison overlapped substantially across runs (4.737–5.574 and 4.502–6.476 s, respectively). The dense person indexes reduced the median grouping and timeline phase from 0.579 to 0.519 s. The 24-byte revision experiment saved approximately 300 MiB at 20M. Streaming Parquet saved about 140 MiB. Both memory-saving candidates increased median elapsed time in their paired experiments.

A separate four-round rotation exercised different ways to remove row-level work. The shared control took 4.692 s. Partitioning by raw alias during parsing, indexing revisions separately, invalidating duplicate locations and combining per-alias timeline fragments took 4.074 s and raised median RSS from 3,360 to 4,189 MiB. Caching exact person routes took 4.213 s. Indexing canonical decimal catalog spellings with a full string-map fallback took 4.419 s and reduced median click parsing and routing from 0.499 to 0.404 s. Removing Parquet statistics took 4.453 s. Combining that with delta binary packing of integer columns took 4.549 s, with median output-phase time of 0.386 s against 0.475 s for the control. Wall times varied enough that the combined finalists were measured again against the scored revision.

A four-round finalist rotation measured 4.957 s for `61e9ba6`, 4.085 s for the worker-split, dense-group parser, 3.757 s after batching hash probes on that parser, 3.663 s for the combined route cache, catalog indexes and Parquet changes, and 3.746 s for the combined implementation with a single-pass AVX2 scanner. Increasing the combined implementation's bounded capacity hint gave 3.674 s and reduced the maximum RSS from 3,373 to 3,223 MiB. The alternative raw-alias architecture took 3.609 s and reached 4,270 MiB. It wrote 97.3 MB at 20M against 43.0 MB for the combined implementation.

The timestamp conversion was also isolated from changes in unrelated phases. Seven alternating measurements of 12 passes over 109,041 real pairs of timestamps produced identical checksums. Median decode time fell from 64.59 to 47.24 ms on translated amd64 and from 21.28 to 15.03 ms on native arm64. The regression test checks every byte substitution at all positions in a 13-byte epoch and in the clock portion of a UTC timestamp, plus every whole second of a day.

On the combined implementation, six further rotated runs gave 3.636 s for the existing probe loop, 3.836 s for batching probes with the larger capacity hint and 512 shards, and 3.662 s with the same changes and 128 shards.

Three additional native 60M runs per variant with Docker Desktop running gave medians of 10.008 s for the selected implementation, 10.483 s for the larger capacity hint, and 10.515 s for batched probes with 128 shards and the larger hint. The selected implementation retains 512 shards, the original bounded capacity hint and the simple probe loop.

### Why 60M can take much longer

The original seed-47 data was regenerated for this investigation. The benchmark-only generator emitted deterministic groups of part files with the original shuffling, row data and headers. A recursive byte comparison of the batched seed-1 generation against the original 1M files passed. The host stored eligible files using transparent APFS compression. Both 60M output tables matched the earlier validated implementation.

For `8b2f568` with eight native workers and the Docker VM offline, the standalone 20M series above had medians of 4.822 user CPU seconds, 0.677 system CPU seconds and one major fault. A three-round 60M rotation had medians of 15.461 user CPU seconds, 6.859 system CPU seconds and 44,242 major faults. Event and click input grew by 2.97×. User CPU grew 3.21×, system CPU grew 10.1× and wall time grew from 0.865 to 5.152 s. Process RSS medians were 4,163 and 8,380 MiB. The 60M run that started with 27% input residency took 21.849 s with 352,215 major faults. Its two runs starting at 100% took 5.131 and 5.152 s. Every 60M process was preceded by a sequential read outside the timer. The Mac could still evict the input before and during a run.

On the same 60M rotation, the dense routing, catalog and delta-encoding candidate took 6.513, 3.825 and 3.655 s (8,906 MiB maximum RSS). The raw-alias fragment design took 5.464, 4.053 and 4.277 s (8,686 MiB maximum RSS). Integer delta encoding reduced the two tables from approximately 288.9 MB to 127.8 MB. The 60M user-day output has 6,684,549 rows against 2,226,509 at 20M, a 3.00× increase. The major faults, system CPU time and starting-residency measurements establish a substantial memory-pressure effect.

Two further controls used the release executable and the same native preload and measurement harness. Each reports three timed runs after one warm-up.

| Native input | Event + click bytes | Link bytes | Median elapsed | Median user CPU | Median system CPU | Parquet bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original seed 23, five runs | 1.899 GB | 18.3 MB | 0.880 s | 4.68 s | 0.73 s | 43.0 MB |
| One third of seed 47 by event/click bytes, all seed-47 links | 1.949 GB | 55.0 MB | 1.498 s | 5.64 s | 1.19 s | 103.4 MB |
| Three copies of seed 23, original links | 5.697 GB logical | 18.3 MB | 3.049 s | 10.71 s | 5.58 s | 43.0 MB |

At comparable input byte counts, the seed-47 subset spent 0.583 s building linked identities against 0.220 s on seed 23. Its grouping and timeline phase took 0.339 s against 0.172 s. This subset contains two event files and two click files, so its composition and file distribution both differ from seed 23. The triplicate control used hard links and processed each event and click part three times. Exact full-table comparison confirmed the original seed-23 results. Its event deduplication and routing rose to 0.692 s while its final merge and Parquet took 0.053 s. Repeated input, a larger identity graph and more output rows each change the work in different phases. For the real seed-47 run under host pressure, 276,794–346,576 major faults and median system CPU of 20.86 s account for much of the excess over the resident controls.

## Reproducing the datasets and checks

The commands below use the original generator and checker from the challenge distribution. In this workspace they are under `generator/` and `starter-pyspark/scripts/`, with the seed-1 answers under `dev-answer/`. The release executable loads Arrow and Parquet C++ libraries. Python is used for building and checking.

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

## Earlier native optimization experiments

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

## 512-shard release verification

* The native executable and the `linux/amd64` submission image passed all 36 published fixtures. Both matched all 117,038 user-day rows and 27,400 campaign-day rows of the independent published dev answer. The amd64 image ran under Docker Desktop using the supplied scorer script with a 7 GiB limit.
* The final 20M output matched the original C++ baseline at 2,226,509 user-day and 28,014 campaign-day rows. The 60M output matched the earlier string-key implementation at 6,684,549 and 28,014 rows. Both diffs reported zero differences and zero duplicate keys. The generated large datasets have no published independent answer here.
* In an alternating five-run native comparison, the original baseline median was 5.865 s and the retained binary median was 2.156 s. In the separate warm shard experiment above the retained binary achieved 0.901 s. Cross-run variation was substantial on this host.
* The standalone 512-shard warm-cache series used two warm-ups followed by seven timed processes: 1.313, 1.084, 0.914, 1.074, 0.874, 0.815 and 0.891 s (median **0.914 s**). These measurements used generated data on the Mac.
* Three final native runs on 60M events and 15M clicks took 17.602, 17.309 and 15.330 s. Peak process RSS from `wait4` was 4.603, 4.723 and 5.216 GiB. These are resident-process measurements on a memory-pressured Mac, not a Linux cgroup high-water mark. The seed-47 CSVs were removed after validation to reclaim space. Use the generator command above to recreate them.

On a separate instrumented warm 20M run, event CSV decoding, validation and sharding took 0.294 s elapsed, including the concurrent 0.198 s identity build. Global deduplication and person routing took 0.155 s, clicks took 0.107 s, grouped timeline processing took 0.212 s, and the final merge and Parquet writes took 0.063 s. The jobs overlap across workers. The diagnostic totals were 0.432 worker-s grouping, 0.477 sorting, 0.075 scanning session boundaries and purchases, 0.088 updating the attribution window, and 0.263 aggregating. Structural CSV scanning accounted for 0.0453 sampled worker-s over 62.4 MB of 1.918 GB. The ordinary C++ allocator handled about 926,000 allocations and 5.51 GB of cumulative requested bytes, with an estimated 0.134 worker-s in malloc. Process launch and teardown are included in the release benchmarks.

## Correctness invariants

* Event ID determines the deduplication shard, including revisions that change user. A CSV-valid but semantically invalid revision participates in earliest-ingest selection. The winning revision must pass validation before entering a person's timeline.
* Compact keys preserve byte identity. Long IDs, unusual prefixes and strings that cannot be represented inline take an exact string-view path. Hash collisions always compare keys. A linked component's display name includes IDs that never appear in events or clicks.
* Repeated identical clicks can remain in the sorted timeline. They preserve the latest timestamp and smallest click ID, and campaign counts contribute one assist while positive. Each person's session and click windows advance monotonically.
* The Parquet diff checks the schema, values, nulls and duplicate keys. The extra randomized parser test compares accepted fields with a scalar decoder and checks compact keys against byte strings. Run it with `c++ -std=c++20 -O2 tests/input.cpp -o build/input-check && build/input-check`.
