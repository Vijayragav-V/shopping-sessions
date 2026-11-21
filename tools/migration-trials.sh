#!/usr/bin/env bash
# Post-change experiment. Compare each build on identical files and CPU affinity.
set -euo pipefail
export NCPU=$(nproc) POLARS_MAX_THREADS=$(nproc)
assets=$GATE_ASSETS
export PYTHONPATH="$assets/starter-pyspark/scripts"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
base=a41989dd4cc92260cce3f5e15e6fd49011cfc256
first=${TRIAL_START:-4d83fa0f8eda106bf507fc2ac9d81f4170059e0d}
host=$(lscpu | sed -n 's/^Model name: *//p')
echo "Host $host; $NCPU vCPUs; boot $(cat /proc/sys/kernel/random/boot_id)"
"$assets/generate" --seed 1 --events 1000000 --clicks 250000 --threads 2 --output "$work/dev"
"$assets/generate" --seed 23 --events 20000000 --clicks 5000000 --threads 2 --output "$work/input"
"$assets/oracle/build/shopping" "$work/input" "$work/ref"
python tools/reference_id.py 23 "$work/ref" > "$work/id"
diff <(grep '^23 ' tools/reference-tables.txt) "$work/id"
mapfile -t commits < <(git rev-list --reverse --first-parent "$first..HEAD" -- cpp build.sh Dockerfile)
binaries=()
for rev in "$base" "${commits[@]}"; do
  # This historical intermediate failed GCC profile matching. The next
  # production commit corrects it; it has no executable to compare.
  if [[ ${rev:0:8} == 289d25ec ]]; then continue; fi
  name=${rev:0:8}
  dir="$work/$name"
  mkdir "$dir"
  git archive "$rev" | tar -xC "$dir"
  docker build --quiet --platform linux/amd64 -t "shopping-trial:$name" "$dir" > /dev/null
  cid=$(docker create "shopping-trial:$name")
  docker cp "$cid:/app/shopping" "$dir/shopping"
  docker rm "$cid" >/dev/null
  echo "Built $name on $host"
  binaries+=("$dir/shopping")
  if [[ $rev == "$base" ]]; then continue; fi
  python "$dir/verify.py" "$dir/shopping" "$work/dev" --starter "$assets/starter-pyspark" --answer "$assets/dev-answer"
  "$dir/shopping" "$work/input" "$work/out"
  python "$assets/starter-pyspark/scripts/compare.py" "$work/out" "$work/ref"
  rm -rf -- "$work/out"
  python "$dir/tests/parquet_pages.py" "$dir/shopping"
  for test in input revision_timelines; do
    c++ -std=c++20 -O2 -pthread "$dir/tests/$test.cpp" -o "$work/unit"
    "$work/unit"
  done
  printf 'Gate A %s PASS\n' "$name"
done
# A single run of the old binary is only part of a post-change comparison.
python -u bench.py "$work/input" "${binaries[@]}" --runs 5 --warmup 1 | tee "$work/screen"
for binary in "${binaries[@]:1}"; do
  id=$(basename "$(dirname "$binary")")
  python -u bench.py "$work/input" "${binaries[0]}" "$binary" --runs 11 --warmup 1 | tee "$work/pair-$id"
done
for report in "$work"/pair-* "$work/screen"; do
  grep ' median ' "$report" | while read -r binary _ secs; do
    echo "::notice title=$(basename "$report")::$(basename "$(dirname "$binary")") $secs ($host)"
  done
done
# The previous stage already paired the Rust binary on compatible hardware.
# Compare the current C++ incumbent and both graph designs at final scale.
rm -rf -- "$work/input" "$work/ref"
"$assets/generate" --seed 47 --events 60000000 --clicks 15000000 --threads 2 --output "$work/final"
"$assets/oracle/build/shopping" "$work/final" "$work/ref"
python tools/reference_id.py 47 "$work/ref" > "$work/id"
diff <(grep '^47 ' tools/reference-tables.txt) "$work/id"
for binary in "${binaries[@]:1}"; do
  "$binary" "$work/final" "$work/out"
  python tools/compare_large.py "$work/out" "$work/ref"
  rm -rf -- "$work/out"
  id=$(basename "$(dirname "$binary")")
  python -u bench.py "$work/final" "${binaries[0]}" "$binary" --runs 11 --warmup 1 | tee "$work/final-$id"
done
