"""Alternate full process runs on one input. Each binary has its own fresh output."""
import argparse
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("input", type=Path)
p.add_argument("binaries", nargs="+", type=Path)
p.add_argument("--runs", type=int, default=5)
p.add_argument("--warmup", type=int, default=0, help="unmeasured runs per executable")
args = p.parse_args()
times = {b: [] for b in args.binaries}
for i in range(-args.warmup, args.runs):
    offset = i % len(args.binaries)
    order = args.binaries[offset:] + args.binaries[:offset]
    for binary in order:
        with tempfile.TemporaryDirectory() as out:
            start = time.perf_counter()
            subprocess.run([binary.resolve(), args.input, out], check=True)
            elapsed = time.perf_counter() - start
        if i < 0:
            continue
        times[binary].append(elapsed)
        print(binary, f"{elapsed:.3f}s", flush=True)
for binary, runs in times.items():
    print(binary, f"median {statistics.median(runs):.3f}s")
