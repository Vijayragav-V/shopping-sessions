# Shopping Sessions in C++

A high-throughput C++20 solution for the Shopping Sessions Buildathon challenge. It reads the challenge CSV dataset, resolves linked identities, deduplicates and validates events and clicks, sessionizes activity, attributes campaigns, and writes the two required Parquet tables.

The complete challenge contract is in [`rules.md`](rules.md).

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

