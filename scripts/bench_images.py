"""Compare two submission images on identical cached input and fresh output."""
from datetime import datetime
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


def timed_run(image, data, output, scratch):
    cpus = os.environ.get("CPUS", "8")
    memory = os.environ.get("MEM", "24g")
    output.mkdir()
    output.chmod(0o777)
    cid = subprocess.check_output([
        "docker", "create", "--platform", "linux/amd64", "--network", "none",
        "--read-only", "--user", "1000:1000", "--cap-drop", "ALL",
        "--security-opt", "no-new-privileges", "--pids-limit", "4096",
        "--tmpfs", "/tmp:rw,size=64m,mode=1777", "--cpus", cpus,
        "--memory", memory, "--memory-swap", memory,
        "-v", f"{data}:/data:ro", "-v", f"{output}:/output",
        "-v", f"{scratch}:/scratch", "-e", "HOME=/scratch",
        "-e", "SCRATCH_DIR=/scratch", "-e", "DATA_DIR=/data",
        "-e", "OUTPUT_DIR=/output", "-e", f"NCPU={cpus}",
        "-e", f"MEM_GB={memory.removesuffix('g')}", image,
    ], text=True).strip()
    try:
        subprocess.run(["docker", "start", "-a", cid], check=True)
        state = json.loads(subprocess.check_output(
            ["docker", "inspect", "--format", "{{json .State}}", cid], text=True))
        if state["ExitCode"] or state["OOMKilled"]:
            raise RuntimeError(f"{image} failed: {state}")
        start, end = (datetime.fromisoformat(state[k].replace("Z", "+00:00"))
                      for k in ("StartedAt", "FinishedAt"))
        return (end - start).total_seconds()
    finally:
        subprocess.run(["docker", "rm", "-f", cid], check=True)
        if os.getuid() not in (0, 1000):
            subprocess.run([
                "docker", "run", "--rm", "--network", "none", "--user", "1000:1000",
                "-v", f"{output}:/output", "-v", f"{scratch}:/scratch",
                "--entrypoint", "find", image, "/output", "/scratch", "-user", "1000",
                "-type", "d", "-exec", "chmod", "a+rwx", "{}", "+",
            ], check=True)


def main():
    data, *images = sys.argv[1:]
    if len(images) != 2 or images[0] == images[1]:
        sys.exit("usage: bench_images.py INPUT ORIGINAL_IMAGE CANDIDATE_IMAGE")
    runs = {image: [] for image in images}
    latest = {}
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        for trial in range(4):
            for i, image in enumerate(images if trial % 2 == 0 else images[::-1]):
                output = root / f"output-{trial}-{i}"
                scratch = root / f"scratch-{trial}-{i}"
                scratch.mkdir()
                scratch.chmod(0o777)
                seconds = timed_run(image, Path(data).resolve(), output, scratch)
                if trial:
                    runs[image].append(seconds)
                latest[image] = output
                print(f"{image} trial {trial} (0 = warmup): {seconds:.3f}s", flush=True)
        subprocess.run([sys.executable, Path(__file__).with_name("compare.py"),
                        *(latest[image] for image in images)], check=True)
    for image, times in runs.items():
        print(f"{image} median: {statistics.median(times):.3f}s")


if __name__ == "__main__":
    main()
