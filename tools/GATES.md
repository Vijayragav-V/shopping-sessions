# Running the gates

| Trigger | Command |
| --- | --- |
| Gate A on a branch containing this workflow | Push the change to `exp/<topic>` or `verify/<topic>`. To rerun the existing SHA, use **Actions → Shopping Sessions gates → run → Re-run all jobs**. |
| Gate B on that branch | `git commit --allow-empty -m "[gate B] Run pre-merge verification"`, then `git push origin HEAD`. |
| Manual dispatch once the workflow exists on the default branch | `gh workflow run gate.yml --ref verify/quality-gates -f gate=A` (or `gate=B`). GitHub requires the workflow on the default branch to register manual dispatch. |
| B runner | Register a native Linux x64 runner with Docker, eight available CPUs, 24 GiB and the label `scorer-8cpu-24g`. The B scorer job waits for that label after public checks pass. |
| Linux setup | Install Docker, git, curl, unzip, Rust (edition 2024), g++, Python 3.11+, and a Python venv with `polars==2.1.0 pyarrow==21.0.0`. From a full checkout, `export GATE_ASSETS="$(mktemp -d)"`, then `bash tools/prepare-gate.sh`. |
| Local execution | `bash tools/gate.sh A` or `bash tools/gate.sh B`. A builds the submission Dockerfile and extracts its executable. For a native binary under development, set `GATE_BINARY=/absolute/path`; B always also exercises the release image. |
| Speed evidence | On each of two native x86 hosts run `NCPU=8 bash tools/gate.sh speed INPUT BEST_BINARY CANDIDATE_BINARY` and report both medians and all 11 alternating timed pairs per host. |

| Reference | Provenance and checks |
| --- | --- |
| 1M | Organizer's SHA-256-pinned generator, starter checker and published answers. |
| 20M / 60M | Independent historical scored commit `33fb788`; all six original Parquet tables are [recorded as artifacts](https://github.com/Vijayragav-V/shopping-sessions/actions/runs/44921612782). The six canonical SHA-256 fingerprints are in `reference-tables.txt`. |
| Stored Parquet | Set `GATE_REFERENCES` to a directory containing `23/`, `47/`, `733/` with the extracted reference tables. By default each pair is re-created from the pinned oracle and verified against the original recorded fingerprint before the exact diff. |
| Resource use | The public job generates and releases one population at a time. Full 60M comparisons sort and compare one day at a time. Gate A timing includes the release build, dataset generation, reference verification and exact comparisons; environment setup is separate. |
