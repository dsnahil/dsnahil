# LogicScope — Event-Driven Digital Simulation in C++20

LogicScope is a deterministic single-bit digital circuit simulator with four-state values (`0`, `1`, `X`, `Z`), inertial gate delays, rising-edge D flip-flops, explicit tri-state resolution, and JSON/VCD waveform export.

The engine demonstrates event scheduling, value semantics, queue invariants, finite resource budgets, and independent functional verification. It is a gate-graph simulator with a custom format, not a Verilog/SystemVerilog compiler or a standards-complete HDL simulator.

## Scheduler and semantics

Events are ordered by `(physical time, delta cycle, insertion sequence)` in a binary min-heap. Every event at the same time and delta is applied before dependent gates are evaluated. Affected gates are deduplicated and evaluated in stable gate declaration order.

- Positive delay schedules at `time + delay`, delta zero.
- Zero delay schedules in the next delta at the current physical time.
- Gate output transitions use generation numbers. A new target invalidates old pending events without deleting an arbitrary heap element.
- Re-evaluation with the same target retains the existing pending transition. A return to the current output cancels a pulse. A pulse shorter than the delay is rejected; a pulse exactly equal to delay passes under the apply-before-evaluate batching rule.
- Each net is single-driver. Use `TRI` gates feeding an explicit `RESOLVE` gate to model competing drivers. `Z` drivers are ignored by resolution; conflicting driven values produce `X`.
- `BUF` treats `Z` as unknown, while `TRI` may emit `Z`. AND/OR honor controlling values, and XOR propagates unknowns.
- `DFF` samples data on a known `0 → 1` edge. `0 → X/Z` and `X/Z → 1` are ambiguous rising edges and schedule `X`. Output delay uses the same inertial mechanism as other gates. Simultaneous data/clock changes in a batch are sampled after both are applied; this is a documented model choice, not a full HDL event-region implementation.
- A run starts all nets at `X`. Inputs need explicit stimuli if a known value is required. Stimuli beyond the horizon are validated but not scheduled.

Limits bound pending queue entries, processed events, and zero-delay delta cycles. Oscillating circuits fail with a diagnostic instead of running forever. Time arithmetic and event generation counters are checked for overflow.

## Input formats

Circuit:

```text
input data
input enable
input clk
wire bus
wire sampled
gate driver TRI bus 2 data enable
gate reg DFF sampled 1 bus clk
```

Each gate is `gate NAME OP OUTPUT DELAY INPUT...`. `DELAY` is an unsigned integer number of nanoseconds. Supported operations: `BUF NOT AND OR XOR NAND NOR XNOR TRI RESOLVE DFF`. DFF arguments are `data clock`; TRI arguments are `data enable`. Net and gate names allow ASCII letters, digits, and underscores. References can precede net declarations. Inputs cannot be gate outputs. Undriven internal nets, repeated drivers, invalid arities, and undeclared nets are rejected.

Stimulus:

```text
0 data 1
0 enable 1
0 clk 0
10 clk 1
```

Each line is `TIME INPUT VALUE`. Time must be unsigned and value must be one character from `01xXzZ`. Duplicate stimuli for one net at one time are rejected; different inputs may change simultaneously. `#` starts comments in both formats.

## Run and inspect

From the [workspace](../README.md):

```sh
mkdir -p out
./build/logicscope/logicscope logicscope/examples/registered_adder.lsc logicscope/examples/registered_adder.stim --until 40 --vcd out/adder.vcd > out/adder.json
python3 scripts/render_waveform.py out/adder.json --output out/adder.svg
```

The sample adds `a`, `b`, and `cin`, then captures the sum on clock edges. `q` becomes `0` at 11 ns and `1` at 31 ns. [Sample waveform](examples/registered_adder.svg).

JSON preserves both physical time and delta. VCD uses a 1 ns timescale and preserves transition order within a timestamp, but does not encode delta as an independent time dimension. A waveform viewer may visually collapse same-time transitions. Use JSON when delta-level inspection matters.

## Library use

```cpp
const auto circuit = logicscope::parse(netlist_stream);
const auto stimulus = logicscope::parse_stimulus(stimulus_stream, circuit);
const auto trace = logicscope::Simulator(circuit).run(stimulus, 1000);
logicscope::write_vcd(output_stream, circuit, trace);
```

`Simulator` borrows `Circuit`; the circuit must outlive a call to `run`. Each run owns its queue, gate state, and trace. Reusing a circuit across independent simulator calls is safe if the caller does not mutate input containers during the calls. Returned traces are owning values. Runs stop on error and can be replayed from the immutable circuit.

## Performance and limitations

With `E` processed events and peak queue `Q`, heap work costs `O(E log Q)`, plus input fan-in evaluation and sorting of each batch's affected gates. Trace storage is proportional to output transitions; this implementation intentionally retains the full trace in memory up to its event budget. It is not a streaming waveform writer.

Not implemented: vector nets, drive-strength resolution, transport-delay mode, HDL parsing, timing checks, metastability physics, asynchronous resets, multiple HDL scheduling regions, or analog behavior. It is single-threaded by design because preserving a deterministic event order across partitions needs additional synchronization and a validated lookahead model.

Verification includes a generated ripple-carry adder family compared with Python integer addition and a separate parser that replays exported VCD values. This tests complete circuits and export behavior, not just individual gate implementations.
