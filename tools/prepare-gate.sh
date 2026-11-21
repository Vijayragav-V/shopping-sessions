#!/usr/bin/env bash
# Prepare the pinned official inputs and an independent historical reference binary.
set -euo pipefail
: "${GATE_ASSETS:?set GATE_ASSETS to a temporary asset directory}"
mkdir -p "$GATE_ASSETS"
root=$(git rev-parse --show-toplevel)
cd "$GATE_ASSETS"
base=https://www.joinbuilders.org/buildathon/tracks/shopping-sessions/files
for file in generator.zip starter-pyspark.zip dev-answer.zip; do
  curl -fsSL --retry 3 "$base/$file" -o "$file"
done
sha256sum -c <<'SUMS'
13724cadbac5a99d847e27cea911cd2e338ff972b46c2039da87452011bb101e  generator.zip
51f11cb497660d06aa04e25e94647ba6d703d5885eec4156d7fe56fdcfcbfccb  starter-pyspark.zip
7eb8e1f0b1f431d178d28ac9437ce85809c44eb6393fdd42fb59857f30834fa9  dev-answer.zip
SUMS
for file in generator.zip starter-pyspark.zip dev-answer.zip; do unzip -qo "$file"; done
rustc --edition 2024 -O generator/src/main.rs -o generate
mkdir -p oracle
git -C "$root" archive 33fb788bf962900617e8c0455d366f7f0a5d7eeb cpp build.sh | tar -xC oracle
(cd oracle && sh build.sh)
