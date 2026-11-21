#!/bin/sh
# Usage: IMAGE=shopping-cpp:final sh bench-container.sh INPUT FRESH_OUTPUT
set -eu
data=$(cd "$1" && pwd)
mkdir "$2"
out=$(cd "$2" && pwd)
chmod 777 "$out"
set --
if [ "${STAGE_INPUT:-0}" = 1 ]; then
  set -- --tmpfs "/local:rw,size=${STAGE_SIZE:-3g},mode=1777"
fi
docker run --rm --platform "${PLATFORM:-linux/amd64}" --read-only --network none \
  --user 1000:1000 --cap-drop ALL --security-opt no-new-privileges \
  --tmpfs /tmp:rw,size=64m,mode=1777 --cpuset-cpus "${CPUSET:-0-7}" --cpus 8 \
  --memory "${MEM:-24g}" --memory-swap "${MEM:-24g}" \
  "$@" -v "$data:/data:ro" -v "$out:/output" -e NCPU=8 -e RUNS="${RUNS:-3}" \
  -e PROFILE -e STAGE_INPUT="${STAGE_INPUT:-0}" \
  --entrypoint /bin/sh "${IMAGE:-shopping-cpp:final}" -ec '
    if [ "$STAGE_INPUT" = 1 ]; then
      cp -R /data/. /local/
      export DATA_DIR=/local
    fi
    run=1
    while [ "$run" -le "$RUNS" ]; do
      export OUTPUT_DIR="/output/run-$run"
      start=$(date +%s%N)
      /app/shopping
      end=$(date +%s%N)
      echo "run $run: $(( (end-start)/1000000 )) ms"
      run=$((run+1))
    done'
