#!/usr/bin/env bash
# A, B, or the two disjoint B jobs used by Actions. See tools/GATES.md.
set -euo pipefail
mode=${1:-A}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
if [[ "$mode" == speed ]]; then
  [[ $# == 4 ]] || { echo "usage: gate.sh speed INPUT BEST_BINARY CANDIDATE_BINARY" >&2; exit 2; }
  [[ $(uname -m) == x86_64 && ! "$3" -ef "$4" ]] || exit 2
  exec python3 bench.py "$2" "$3" "$4" --runs 11 --warmup 1
fi
case "$mode" in A|B|B-public|B-scorer) ;; *) echo "expected A or B" >&2; exit 2;; esac
: "${GATE_ASSETS:?prepare the official assets as described in tools/GATES.md}"
assets=$(cd "$GATE_ASSETS" && pwd)
starter="$assets/starter-pyspark"
export PYTHONPATH="$starter/scripts${PYTHONPATH:+:$PYTHONPATH}"
unset PYTHONOPTIMIZE
export NCPU=${NCPU:-4}
export POLARS_MAX_THREADS=${POLARS_MAX_THREADS:-4}
tmp=$(mktemp -d)
tmp=$(cd "$tmp" && pwd)
image="shopping-gate:$(basename "$tmp" | tr '[:upper:]' '[:lower:]')"
cid=
image_built=0
cleanup() {
  if [[ -n "$cid" ]]; then docker rm "$cid" >/dev/null || true; fi
  if [[ -d "$tmp/container/run-1" ]]; then
    docker run --rm --network none --user 1000:1000 -v "$tmp/container:/out" \
      --entrypoint find "$image" /out -uid 1000 -type d -exec chmod a+rwx '{}' + >/dev/null || true
  fi
  if (( image_built )); then docker image rm "$image" >/dev/null || true; fi
  rm -rf -- "$tmp"
}
trap cleanup EXIT
report() {
  printf '| %s | %s | %s |\n' "$1" "$2" "$3"
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    printf '| %s | %s | %s |\n' "$1" "$2" "$3" >> "$GITHUB_STEP_SUMMARY"
  fi
}
report Check Result Seconds
report --- --- ---:
started=$SECONDS
generate() {
  "$assets/generate" --seed "$1" --events "$2" --clicks "$(($2 / 4))" --threads 2 --output "$3"
}
build_image() {
  docker build --platform linux/amd64 -t "$image" .
  image_built=1
}
candidate() {
  if [[ -n "${GATE_BINARY:-}" ]]; then binary=$(realpath "$GATE_BINARY"); test -x "$binary"; return; fi
  build_image
  cid=$(docker create "$image")
  docker cp "$cid:/app/shopping" "$tmp/shopping"
  docker rm "$cid" >/dev/null; cid=
  binary="$tmp/shopping"
}
reference() {
  local seed=$1 data=$2
  if [[ -n "${GATE_REFERENCES:-}" ]]; then
    expected="$GATE_REFERENCES/$seed"
    test -d "$expected" || { echo "missing recorded reference: $expected" >&2; return 1; }
  else
    expected="$tmp/recreated-$seed"
    "$assets/oracle/build/shopping" "$data" "$expected"
  fi
  python3 tools/reference_id.py "$seed" "$expected" > "$tmp/actual-id"
  awk -v seed="$seed" '$1==seed' tools/reference-tables.txt > "$tmp/expected-id"
  test "$(wc -l < "$tmp/expected-id")" -eq 2
  diff -u "$tmp/expected-id" "$tmp/actual-id"
}
large() {
  local seed=$1 size=$2 begin=$SECONDS data="$tmp/data-$1"
  generate "$seed" "$size" "$data"
  reference "$seed" "$data"
  "$binary" "$data" "$tmp/result"
  if [[ $seed == 23 ]]; then
    python3 "$starter/scripts/compare.py" "$tmp/result" "$expected"
  else
    python3 tools/compare_large.py "$tmp/result" "$expected"
  fi
  rm -rf -- "$data" "$tmp/result" "$tmp/recreated-$seed"
  report "Seed $seed ($size events, 2 tables)" PASS "$((SECONDS-begin))"
}
gate_a() {
  candidate
  test "$(find "$starter/fixtures" -mindepth 1 -maxdepth 1 -type d | wc -l)" -eq 36
  generate 1 1000000 "$tmp/dev"
  python3 verify.py "$binary" "$tmp/dev" --starter "$starter" --answer "$assets/dev-answer"
  report "36 fixtures and 2 published 1M tables" PASS "$((SECONDS-started))"
  large 23 20000000
  report "Gate A total" PASS "$((SECONDS-started))"
}
sanitizers() {
  local kind flags name begin
  for kind in thread address,undefined; do
    begin=$SECONDS
    flags=(-std=c++20 -O1 -g -fno-omit-frame-pointer -pthread "-fsanitize=$kind" -fno-sanitize-recover=all)
    export NCPU=8 TSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1
    for name in input revision_timelines; do
      "${SAN_CXX:-g++}" "${flags[@]}" "tests/$name.cpp" -o "$tmp/$name"
      "$tmp/$name"
    done
    "${SAN_CXX:-g++}" "${flags[@]}" cpp/main.cpp -o "$tmp/sanitized"
    python3 verify.py "$tmp/sanitized" "$tmp/dev" --starter "$starter" --answer "$assets/dev-answer"
    python3 tests/parquet_pages.py "$tmp/sanitized"
    report "$kind sanitizer; 8 workers on dev" PASS "$((SECONDS-begin))"
  done
}
scorer() {
  local begin=$SECONDS
  [[ $(uname -s) == Linux && $(uname -m) == x86_64 ]]
  read -r cpus memory < <(docker info --format '{{.NCPU}} {{.MemTotal}}')
  (( cpus >= 8 && memory >= 24 * 1024**3 )) || { echo "Scorer gate requires 8 CPUs and 24 GiB on the Docker host" >&2; return 1; }
  CPUSET=$(python3 -c 'import os; c=sorted(os.sched_getaffinity(0)); assert len(c)>=8; print(",".join(map(str,c[:8])))')
  export CPUSET
  if (( ! image_built )); then build_image; fi
  generate 23 20000000 "$tmp/scorer-data"
  reference 23 "$tmp/scorer-data"
  IMAGE="$image" MEM=24g RUNS=1 STAGE_INPUT=0 PLATFORM=linux/amd64 \
    sh bench-container.sh "$tmp/scorer-data" "$tmp/container"
  python3 "$starter/scripts/compare.py" "$tmp/container/run-1" "$expected"
  report "Docker scorer, 20M, 2 tables" PASS "$((SECONDS-begin))"
}
if [[ $mode != B-scorer ]]; then gate_a; fi
if [[ $mode == B || $mode == B-public ]]; then
  large 47 60000000
  large 733 60000000
  sanitizers
fi
if [[ $mode == B || $mode == B-scorer ]]; then scorer; fi
report "$mode complete" PASS "$((SECONDS-started))"
