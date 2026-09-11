"""Run reproducible in-process C++ benchmarks and retain raw samples and context."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

p = argparse.ArgumentParser()
p.add_argument("executable", type=Path)
p.add_argument("--nodes", type=int, default=50000)
p.add_argument("--runs", type=int, default=5)
p.add_argument("--output", type=Path, default=Path("out/benchmark.json"))
args = p.parse_args()
if args.runs < 1:
    p.error("--runs must be positive")
exe = args.executable.resolve()

def run():
    return json.loads(subprocess.check_output([str(exe), str(args.nodes)], text=True, timeout=120))

run()  # Explicit warm-up, excluded from samples.
samples = [run() for _ in range(args.runs)]
info = {
    "measured_at_utc": datetime.now(timezone.utc).isoformat(),
    "platform": platform.platform(),
    "cpu_count_visible": os.cpu_count(),
    "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
    "build_type": "Release (configure explicitly before running)",
    "warmup_runs": 1,
    "measured_runs": args.runs,
    "scope": "Wall time inside native process; includes result/trace allocations, excludes parsing and JSON export. Graph construction excluded. No commercial-tool comparison.",
    "samples": samples,
    "median": {key: statistics.median(s[key] for s in samples) for key in samples[0]},
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(info, indent=2) + "\n")
print(json.dumps(info["median"], indent=2))
