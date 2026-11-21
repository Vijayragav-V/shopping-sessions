from datetime import date
import gc
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tarfile
import tempfile
import time

repo = Path.cwd()
lab = Path(os.environ["RUNNER_TEMP"]) / "lab"
lab.mkdir()
available = sorted(os.sched_getaffinity(0))[:8]
os.sched_setaffinity(0, available)
os.environ.update(NCPU=str(len(available)), POLARS_MAX_THREADS=str(len(available)), SCORER_CPU="1")
tmp = lab.parent
entries = [tuple(line.split()) for line in (repo/".github/perf/revisions.txt").read_text().splitlines() if line and not line.startswith("#")]
labels = [name for name, sha in entries]
assert labels[0] == "baseline"
def note(title, data):
    message = str(data).replace("%", "%25").replace("\n","%0A").replace("\r","%0D")
    print(f"::notice title={title}::{message}",flush=True)
def run(*args, **kw):
    return subprocess.run(args, check=True, **kw)
def timed(exe, data, out, profile=False):
    # Measure a complete, fresh process. Capture stderr after process exit.
    env = dict(os.environ)
    if profile:
        env["PROFILE"] = "1"
    start = time.perf_counter()
    command = ["/usr/bin/time", "-f", "peak RSS %M KiB"] if profile else []
    p = run(*command, exe, data, out, env=env, stderr=subprocess.PIPE, text=True)
    return time.perf_counter()-start, p.stderr.strip()
def generate(seed, n, path):
    run(tmp/"generate","--seed",str(seed),"--events",str(n),"--clicks",str(n//4),"--threads","2","--output",path)
binaries = {}
for label, sha in entries:
    loc = lab/label
    loc.mkdir()
    archive = loc/"src.tar"
    run("git","archive",sha,"--output",str(archive),"cpp","build.sh","tests")
    with tarfile.open(archive) as tf:
        tf.extractall(loc, filter="data")
    run("sh","build.sh",cwd=loc)
    run("c++","-std=c++20","-O3","-march=x86-64-v3","tests/input.cpp","-o","build/parser-test",cwd=loc)
    run(loc/"build/parser-test")
    binaries[label] = loc/"build/shopping"
target = subprocess.check_output(["git","log","-1","--format=%B"],text=True)
sizes = [(23,20_000_000)]
if "[60M]" in target:
    sizes.append((47,60_000_000))
dev = lab/"dev"
generate(1,1_000_000,dev)
for label in labels:
    run(sys.executable,repo/"verify.py",binaries[label],dev,"--answer",tmp/"dev-answer")
    run(sys.executable,repo/"tests/compact_cases.py",binaries[label])
    note(label+" correctness","36 fixtures and both published dev tables matched")
for seed, size in sizes:
    data = lab/f"data-{seed}"
    generate(seed,size,data)
    note("host", f"{size} events, {size//4} clicks, CPUs {available}, " + subprocess.check_output(["bash","-c","lscpu | grep 'Model name' | head -1"],text=True).strip())
    reference = lab/f"reference-{seed}"
    for i,label in enumerate(labels):
        output = lab/f"verify-{seed}-{label}" if i else reference
        wall, phases = timed(binaries[label],data,output,True)
        note(label+" phases "+str(size), f"release wall {wall:.4f}s\n"+phases)
        if i:
            script = "scripts/compare.py" if size == 20_000_000 else ".github/perf/compare_large.py"
            run(sys.executable, repo/script, output,reference)
            shutil.rmtree(output)
    if "[profile]" in target and size == 20_000_000:
        for label in dict.fromkeys([labels[0],labels[-1]]):
            loc=lab/label
            run("sh","build.sh",cwd=loc,env=dict(os.environ,DIAGNOSTICS="1"))
            out=lab/("diag-"+label)
            wall, diag=timed(loc/"build/shopping-profile",data,out,True)
            note(label+" diagnostic worker time",diag)
            shutil.rmtree(out)
    series = {label:[] for label in labels}
    for trial in range(6 if size==20_000_000 else 4):
        # Rotate release order; every binary has its own fresh output.
        order=labels[trial%len(labels):]+labels[:trial%len(labels)]
        for label in order:
            out=lab/f"trial-{label}-{trial}"
            wall,_=timed(binaries[label],data,out)
            if trial:
                series[label].append(round(wall,5))
            shutil.rmtree(out)
    for label,vals in series.items():
        note(f"{size} {label}",f"median {statistics.median(vals):.4f}s; trials {vals}")
    shutil.rmtree(data);shutil.rmtree(reference)
