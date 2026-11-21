# Shopping Sessions in C++

A high-throughput C++20 solution for the Shopping Sessions Buildathon challenge. It reads the challenge CSV dataset, resolves linked identities, deduplicates and validates events and clicks, sessionizes activity, attributes campaigns, and writes the two required Parquet tables.

The complete challenge contract is in [`rules.md`](rules.md).

The starting implementation is [mwkhettab/shopping-sessions-cpp at 8b2f568](https://github.com/mwkhettab/shopping-sessions-cpp/tree/8b2f568600d0fb5cc9bc066dc13ce609ed5d0e7b). The scoring image scans CSV with AVX2, batches linked-user lookups, and packs events and clicks when their values fit. For long timelines, 30-minute bins track the first and last event and the event count. These boundaries recover exact sessions while only purchases need sorting.

## Build the submission image

The scorer runs `linux/amd64`. Build the same image locally with Docker Buildx:

```sh
docker buildx build --platform linux/amd64 --load \
  -t shopping-sessions-cpp:latest .
```

On Apple silicon, use `--platform linux/arm64` for faster development builds, then validate an `amd64` image before submitting.

## Run

The image reads `/data` and writes `/output` by default. The scorer supplies eight CPUs and 24 GB of memory, with no network access at runtime.

```sh
mkdir -p output scratch
docker run --rm \
  --cpus=8 --memory=24g --network=none --read-only \
  -v "$PWD/dev:/data:ro" \
  -v "$PWD/output:/output" \
  -v "$PWD/scratch:/scratch" \
  -e DATA_DIR=/data \
  -e OUTPUT_DIR=/output \
  -e SCRATCH_DIR=/scratch \
  -e HOME=/scratch \
  -e NCPU=8 \
  -e MEM_GB=24 \
  shopping-sessions-cpp:latest
```

The result is written as Parquet files under:

- `output/user_daily/`
- `output/campaign_daily/`

## Native development build

The native build expects Python 3 with `pyarrow==21.0.0`; the build script links against the Arrow and Parquet libraries bundled in that wheel.

```sh
python3 -m pip install pyarrow==21.0.0
sh build.sh
DATA_DIR=dev OUTPUT_DIR=output NCPU=8 ./build/shopping
```

## Implementation notes

- Memory-mapped, SIMD-assisted CSV parsing
- Exact linked-identity resolution
- Sharded event deduplication and person grouping
- Parallel sessionization and campaign attribution
- Direct Arrow/Parquet output

## Verification

With the organizer's generator on your path, unpack the public answer into
`dev-answer/` and run:

```sh
python3 -m pip install polars==2.1.0
make dev
python3 verify.py build/shopping dev
```

The Linux image check builds the Dockerfile, runs all 36 official fixtures and three compact-storage regression cases, and compares
both public dev tables with the organizer's answers. Running the check manually,
or including `[benchmark]` in a commit message, also times the prior 1.65-second submission (c94a3b5) and
the current image on generated seed 23 (20M events and 5M clicks). It reports
Docker's start-to-exit time for each fresh container, alternates run order and
compares both output tables. The historical measurements in `BENCHMARKS.md`
belong to the upstream implementation.


## Measured performance

On four pinned Linux vCPUs, five fresh-process trials on seed 23 (20M events, 5M clicks) had a median of 1.742 s versus 2.149 s for c94a3b5. At final size on seed 47 (60M events, 15M clicks), three trials had a median of 5.177 s versus 6.516 s. The optimized process peaked at 8.28 GiB on the larger input. Both output tables matched exactly at both sizes. The official scorer uses eight vCPUs.

In one 20M run, event parsing, validation and partitioning took 0.747 s, including 0.228 s of concurrent identity resolution. Event deduplication and routing took 0.290 s, click processing 0.194 s, grouping through aggregation 0.306 s, and output 0.125 s. [Paired timings and memory measurements](https://github.com/Vijayragav-V/shopping-sessions/actions/runs/44912318835) Â· [Allocation and worker profiles](https://github.com/Vijayragav-V/shopping-sessions/actions/runs/44875462635)
