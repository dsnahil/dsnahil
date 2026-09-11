"""Render LogicScope JSON as a self-contained SVG; no external assets or packages."""
import argparse
import html
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("trace", type=Path)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--nets", nargs="*")
args = p.parse_args()
data = json.loads(args.trace.read_text())
nets = args.nets if args.nets else list(data["final"])[:24]
if not nets or len(nets) > 32 or any(n not in data["final"] for n in nets):
    p.error("select between 1 and 32 existing nets")
end = max((t["time_ns"] for t in data["transitions"]), default=0) + 5
width, left, right, top, row = 1080, 120, 40, 100, 48
height = top + len(nets) * row + 50
scale = (width - left - right) / max(1, end)
def x(time):
    return left + time * scale

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
       '<rect width="100%" height="100%" rx="16" fill="#101725"/>',
       '<g font-family="monospace" fill="#dce7f6">',
       '<text x="32" y="38" font-size="24" font-weight="bold">LogicScope / waveform trace</text>',
       '<text x="32" y="63" font-size="13" fill="#94a8c4">Four-state logic · physical time in ns · same-time deltas may overlap</text>']
for time in range(0, int(end) + 1, max(1, int(end) // 8)):
    svg += [f'<path d="M{x(time):.2f} 90 V{height-35}" stroke="#253044"/>',
            f'<text x="{x(time):.2f}" y="84" font-size="12">{time}</text>']
for i, net in enumerate(nets):
    y = top + i * row
    svg.append(f'<text x="20" y="{y+23}" font-size="15">{html.escape(net)}</text>')
    transitions = [(0, "x")] + [(t["time_ns"], t["value"]) for t in data["transitions"] if t["net"] == net]
    for j, (time, value) in enumerate(transitions):
        stop = transitions[j+1][0] if j+1 < len(transitions) else end
        if value in "01":
            level = y+6 if value == "1" else y+32
            svg.append(f'<path d="M{x(time):.2f} {level} H{x(stop):.2f}" stroke="#62dbc2" stroke-width="2"/>')
            if j and transitions[j-1][1] in "01" and transitions[j-1][1] != value:
                svg.append(f'<path d="M{x(time):.2f} {y+6} V{y+32}" stroke="#62dbc2" stroke-width="2"/>')
        else:
            color = "#f7b76a" if value == "x" else "#a998f4"
            svg.append(f'<path d="M{x(time):.2f} {y+19} H{x(stop):.2f}" stroke="{color}" stroke-width="2" stroke-dasharray="4 3"/>')
            if stop > time:
                svg.append(f'<text x="{(x(time)+x(stop))/2:.2f}" y="{y+13}" fill="{color}" font-size="12">{value.upper()}</text>')
svg += ['</g></svg>']
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(svg) + "\n")
print(args.output)
