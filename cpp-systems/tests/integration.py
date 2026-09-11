"""Independent CLI-level reference checks. Standard-library Python only."""
import itertools
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile

TIMING, LOGIC = map(str, map(Path, sys.argv[1:3]))
rng = random.Random(20260910)


def invoke(exe, *args, good=True):
    p = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=15)
    if good:
        assert p.returncode == 0, p.stderr
        return json.loads(p.stdout)
    assert p.returncode != 0 and p.stderr, (p.returncode, p.stdout)


def enumerate_paths(n, arcs, node):
    # Exhaustive recursion, deliberately different from the engine's topological DP.
    incoming = [(u, lo, hi) for u, v, lo, hi in arcs if v == node]
    if not incoming:
        return [(0.0, 0.25)]
    return [(early + lo, late + hi) for u, lo, hi in incoming
            for early, late in enumerate_paths(n, arcs, u)]


def netlist(n, arcs):
    return ("input n0 0 0.25\n" + "".join(f"node n{i}\n" for i in range(1, n - 1))
            + f"output n{n-1} 1 20\n"
            + "".join(f"arc n{u} n{v} {lo} {hi}\n" for u, v, lo, hi in arcs))


def timing_checks(root):
    file, eco = root / "case.tfg", root / "case.eco"
    for _ in range(120):
        n = rng.randint(3, 11)
        arcs = []
        for v in range(1, n):
            for u in sorted(rng.sample(range(v), rng.randint(1, min(3, v)))):
                lo = rng.randrange(0, 8) / 4
                arcs.append((u, v, lo, lo + rng.randrange(0, 10) / 4))
        file.write_text(netlist(n, arcs))
        result = invoke(TIMING, file)["endpoints"][0]
        paths = enumerate_paths(n, arcs, n - 1)
        early, late = min(p[0] for p in paths), max(p[1] for p in paths)
        assert math.isclose(result["earliest_ns"], early)
        assert math.isclose(result["latest_ns"], late)
        assert math.isclose(result["hold_slack_ns"], early - 1)
        assert math.isclose(result["setup_slack_ns"], 20 - late)
        # Verify every reported path is a real path with the claimed delay.
        for key, time, component in [("early_path", early, 2), ("late_path", late, 3)]:
            nodes = [int(s[1:]) for s in result[key]]
            assert nodes[0] == 0 and nodes[-1] == n - 1
            total = 0 if component == 2 else 0.25
            for a, b in zip(nodes, nodes[1:]):
                total += next(e[component] for e in arcs if e[:2] == (a, b))
            assert math.isclose(total, time)
        changed = rng.sample(range(len(arcs)), min(3, len(arcs)))
        updates = []
        for i in changed:
            u, v, _, _ = arcs[i]
            lo = rng.randrange(0, 10) / 4
            hi = lo + rng.randrange(0, 10) / 4
            arcs[i] = (u, v, lo, hi)
            updates.append(f"set {i} {lo} {hi}\n")
        eco.write_text("".join(updates))
        incremental = invoke(TIMING, file, "--eco", eco)
        file.write_text(netlist(n, arcs))
        full = invoke(TIMING, file)
        assert incremental["endpoints"] == full["endpoints"]
    # Independent path calculations checked at all three delay scales.
    scales = [0.5, 1, 2]
    result = invoke(TIMING, file, "--corners", *scales, "--workers", 3)
    for scale, corner in zip(scales, result["corners"]):
        p = enumerate_paths(n, [(a, b, lo * scale, hi * scale) for a, b, lo, hi in arcs], n - 1)
        assert math.isclose(corner["result"]["endpoints"][0]["latest_ns"], max(x[1] for x in p))
    print("PASS 120 randomized DAGs: exhaustive path oracle + batched ECO/rebuild equivalence")


def truth(op, v):
    if op in ("AND", "NAND"):
        value = "0" if "0" in v else "x" if any(x in "xz" for x in v) else "1"
    elif op in ("OR", "NOR"):
        value = "1" if "1" in v else "x" if any(x in "xz" for x in v) else "0"
    else:
        value = "x" if any(x in "xz" for x in v) else str(sum(map(int, v)) % 2)
    return {"0": "1", "1": "0", "x": "x"}[value] if op in ("NAND", "NOR", "XNOR") else value


def truth_checks(root):
    net, stim = root / "truth.lsc", root / "truth.stim"
    ops = ["AND", "OR", "XOR", "NAND", "NOR", "XNOR"]
    net.write_text("input a\ninput b\ninput c\n" + "".join(f"wire y{o}\ngate g{o} {o} y{o} 0 a b c\n" for o in ops))
    # One run covers every input combination; replay transitions into checkpoints.
    cases = list(itertools.product("01xz", repeat=3))
    stim.write_text("".join(f"{i*10} {n} {v}\n" for i, values in enumerate(cases) for n, v in zip("abc", values)))
    result = invoke(LOGIC, net, stim, "--until", 1000)
    state = {name: "x" for name in ["a", "b", "c", *("y"+o for o in ops)]}
    changes = iter(result["transitions"])
    pending = next(changes, None)
    for i, values in enumerate(cases):
        while pending and pending["time_ns"] <= i * 10:
            state[pending["net"]] = pending["value"]
            pending = next(changes, None)
        for op in ops:
            assert state["y" + op] == truth(op, values), (op, values, state)
    print("PASS 384 exhaustive three-input four-state truth-table comparisons")


def adder_netlist(bits):
    s = "input c0\n"
    for i in range(bits):
        s += f"input a{i}\ninput b{i}\nwire x{i}\nwire p{i}\nwire g{i}\nwire s{i}\nwire c{i+1}\n"
        s += f"gate xor{i} XOR x{i} 1 a{i} b{i}\ngate gen{i} AND g{i} 1 a{i} b{i}\n"
        s += f"gate prop{i} AND p{i} 1 x{i} c{i}\ngate sum{i} XOR s{i} 1 x{i} c{i}\ngate carry{i} OR c{i+1} 1 g{i} p{i}\n"
    return s


def adder_checks(root):
    net, stim, vcd = root / "adder.lsc", root / "adder.stim", root / "adder.vcd"
    for bits in (2, 4, 8, 16):
        net.write_text(adder_netlist(bits))
        cases = [(rng.randrange(1 << bits), rng.randrange(1 << bits), rng.randrange(2)) for _ in range(80)]
        lines = []
        for i, (a, b, cin) in enumerate(cases):
            lines.append(f"{i*100} c0 {cin}\n")
            for bit in range(bits):
                lines += [f"{i*100} a{bit} {(a>>bit)&1}\n", f"{i*100} b{bit} {(b>>bit)&1}\n"]
        stim.write_text("".join(lines))
        result = invoke(LOGIC, net, stim, "--until", 8000, "--vcd", vcd)
        state = {}
        changes = iter(result["transitions"]); pending = next(changes, None)
        for i, (a, b, cin) in enumerate(cases):
            while pending and pending["time_ns"] < (i + 1) * 100:
                state[pending["net"]] = pending["value"]; pending = next(changes, None)
            actual = sum(int(state[f"s{bit}"]) << bit for bit in range(bits)) + (int(state[f"c{bits}"]) << bits)
            assert actual == a + b + cin, (bits, a, b, cin, actual)
        # Validate VCD net declarations and replay its values independently.
        names, values = {}, {}
        for line in vcd.read_text().splitlines():
            if line.startswith("$var"):
                tokens = line.split(); names[tokens[3]] = tokens[4]
            elif line and line[0] in "01xz" and line[1:] in names:
                values[names[line[1:]]] = line[0]
        assert values == result["final"]
    print("PASS 320 arithmetic-oracle vectors across 2/4/8/16-bit adders + VCD replay")


def invalid_checks(root):
    for args in [(TIMING, "missing"), (LOGIC, "missing", "missing")]:
        invoke(*args, good=False)
    bad = root / "bad"
    for text in ("input a 0 0\noutput b 0 1\narc a b nan 1", "input a 0 0\noutput b 0 1\narc a b 2 1"):
        bad.write_text(text); invoke(TIMING, bad, good=False)
    print("PASS CLI failure paths")


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    timing_checks(root)
    truth_checks(root)
    adder_checks(root)
    invalid_checks(root)
