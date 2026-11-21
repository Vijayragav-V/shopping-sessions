# Shopping Sessions

| Requirement | Rule |
| --- | --- |
| Specification | `rules.md` defines both output tables. Preserve every value, null, type required by the spec, and unique daily key. Correctness takes priority over speed. |
| Branches | Work only on `exp/<topic>` or `verify/<topic>`. Never commit to `main`, force-push, or submit anything to the competition. |
| Gate A | For every change, pass all 36 official fixtures, match both published 1M tables, and diff both full 20M tables against the recorded reference. |
| Gate B | Before proposing a branch for merge, pass Gate A, both full 60M diffs (seeds 47 and 733), eight-worker ThreadSanitizer, ASan/UBSan, and a Docker run under scorer limits (read-only root, UID 1000, eight CPUs, 24 GB, no network). |
| Speed claim | On each of at least two x86 hosts, alternate the candidate with the best-known binary on the same input and host for at least 11 timed pairs. Report paired-run counts and medians separately by host. A win on one host is insufficient. |
| Rejected experiments | Check `BENCHMARKS.md` and `DISCARDED.md` before experimenting. Never retry an idea listed as rejected. |
| Reporting | Use tables of numbers for gate results and speed claims. Record the commit, seed, input size, host, worker count, elapsed time and diff result. |
