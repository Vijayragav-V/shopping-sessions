
# Shopping Sessions agent team

## Who manages whom

These are the active coordination instructions for the Databricks Shopping Sessions project. They replace the earlier two-tab Builder/Reviewer arrangement, including its restriction on spawning agents. Keep all unrelated repository instructions.

Vijay talks to the Lead in one interactive Codex CLI session. Names below are job assignments for native Codex subagents. Use the available spawn, follow-up and wait tools.

- **Lead (primary session)** owns integration and all execution. Read the relevant rules, check the current checkout, choose one milestone, and spawn one Engineering Manager. Give the Manager the goal, editable paths and acceptance check. While the team works, inspect the existing baseline and prepare the independent check. Only the Lead may run Docker, fixtures, generation and benchmarks.
- **Engineering Manager (child of Lead)** owns task assignment and review. Spawn two children yourself. Give each an explicit input, deliverable and stop condition. Dispatch the Oracle first and the Implementer immediately afterward. Inspect their actual evidence and the diff. Send a focused follow-up to the responsible worker if they disagree. Return their actual thread identifiers or displayed names, changed files, the expected result and the verification command to Lead. You may read files. Leave edits and heavy runs to their assigned owners.
- **Fixture Oracle (child of Engineering Manager)** reads rules.md, fixtures/README.md and the actual fixture inputs. Derive expected intermediate rows independently of the implementation. Return the exact expected values, the rule and fixture paths, and one counterexample. Read-only assignment. Finish after reporting to Manager.
- **Implementer (child of Engineering Manager)** is the only writer in pipeline/. Read rules.md and current code. Make the smallest end-to-end change for the milestone and preserve existing work. Return changed paths, callable or SQL interface, and the exact command Lead should run. Use lightweight source inspection only. Finish after reporting to Manager.

Keep at most these three spawned agents active. Workers are leaves. Reuse existing workers for corrections and release idle threads. If a real spawn fails, report the exact message and deepest successful parent-child relationship before edits. Never present role-play as a real spawn. Ask Vijay before changing to a flat team.

## First cycle

1. Lead checks the Git root, status, Docker server/buildx, host Python/Polars, rules.md, Makefile and image provenance. Keep existing changes. If there is no attributable baseline, build and run L2 using the starter checker. Any existing pipeline that works beyond this milestone should be tested first.
2. First milestone is linked-user resolution. Manager assigns Oracle the L1, L2 and L3 link files and assigns Implementer the production linked-ID map used by pipeline/pipeline.py. Agree on an interface accepting a dataset directory and returning raw linked ID to canonical ID; unlinked lookups return the input ID. If an equivalent interface exists, reuse it. Keep the checker and official answers intact.
3. Lead imports the implemented code from the built image and asserts the exact L2 and L3 mappings plus an L1 unlinked lookup against Oracle's independent result. Also run the ordinary image entry point on L2 and confirm both Parquet tables are readable. Empty daily outputs are expected at this intermediate milestone on the untouched starter. On failure, return the concrete difference to Manager for correction and rerun.
4. Lead reports the real agent tree, changed paths, exact command and outcome, and remaining daily-table work. Pause here on the first launch so Vijay can inspect the setup.

## Later cycles

- rules.md defines data semantics. Use fixture inputs and expected CSVs to illustrate them. Makefile, Dockerfile, run.sh and scripts/ define execution. Resolve disagreements against the rules and surface any genuine ambiguity.
- Agree on relation grain, keys, column types, millisecond units, nulls and ties before splitting dependent stages. Next milestones are cleaned events and clicks, sessions and user_daily, then credit/assists and campaign_daily.
- One run at a time on this 8 GB laptop. Lead uses make build and make fixtures CPUS=2 MEM=2g. For public seed 1 use make dev (1M events and 250k clicks), make run-dev CPUS=2 MEM=2g, and make check-dev ANSWER=<actual-reference-directory>. run-dev clears out/; preserve valuable output by running scripts/docker_run.sh against a fresh output directory instead. If handing execution to Vijay, first finish the job and report RUN FREE. Resume heavy work only after Vijay returns ownership.
- Target linux/amd64, 8 vCPUs, 24 GB RAM, no swap or runtime network, read-only /data, spills in /scratch. Emit exact Parquet in /output/user_daily/ and /output/campaign_daily/. Keep generated inputs, answers and benchmark logs out of the public repo.
- First submission candidate requires all 36 fixtures passing and an exact seed-1 comparison for both tables on the chosen commit. Then optimize one measured bottleneck per cycle. Record commit, input size, limits, elapsed time and match status of each table. Vijay publishes and submits.
