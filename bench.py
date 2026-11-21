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
args = p.parse_args()
times = {b: [] for b in args.binaries}
for i in range(args.runs):
    order = args.binaries if i % 2 == 0 else args.binaries[::-1]
    for binary in order:
        with tempfile.TemporaryDirectory() as out:
            start = time.perf_counter()
            subprocess.run([binary.resolve(), args.input, out], check=True)
            elapsed = time.perf_counter() - start
        times[binary].append(elapsed)
        print(binary, f"{elapsed:.3f}s", flush=True)
for binary, runs in times.items():
    print(binary, f"median {statistics.median(runs):.3f}s")
