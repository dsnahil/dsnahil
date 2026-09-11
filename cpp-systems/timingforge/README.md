# TimingForge — Incremental Timing Analysis in C++20

TimingForge computes earliest and latest arrival times through a directed acyclic timing graph. It reports endpoint setup/hold slack and the actual paths responsible for both, supports batched delay edits, and analyzes delay scales using a bounded group of C++20 `std::jthread` workers.

This is an independently implemented, window-based timing model. Its focus is graph-engine correctness and C++ systems design. It does not parse Liberty, Verilog, SPEF, or SDC and must not be used for silicon signoff.

## Useful design details

- Validated immutable graph with **compressed sparse row** incoming and outgoing arc indexes.
- Iterative topological construction and propagation: no recursion proportional to circuit depth.
- Stable arc IDs and deterministic tie-breaking for reproducible critical paths.
- Early/late forward propagation and min/max backward required-time propagation.
- A topological-priority worklist visits affected forward nodes after a delay change; propagation stops when arrival values are unchanged.
- Delay-update validation occurs before mutation. State snapshots restore a usable engine if propagation overflows or allocation fails after mutation begins.
- Independent per-corner state prevents data sharing races. The immutable graph is shared through `std::shared_ptr<const Graph>`; the worker counter uses a relaxed atomic only for unique job assignment.
- Strict parser diagnostics, finite-number validation, duplicate/cycle rejection, and JSON suitable for downstream automation.

## Input model

All times are nanoseconds; arc IDs follow zero-based declaration order. Declare each node exactly once. Input declarations may appear before or after arcs, but every reference must resolve when parsing completes.

```text
input a 0 0.2          # earliest and latest input arrivals
input b 0.1 0.4
node logic
output y 0.8 3.5      # minimum allowed arrival (hold), maximum allowed arrival (setup)
arc a logic 0.2 0.7   # minimum and maximum arc delay
arc b logic 0.3 0.9
arc logic y 0.5 1.2
```

Names allow ASCII letters, digits, `_`, `.`, and `/`. Primary inputs cannot have incoming arcs. Every source needs an input-arrival constraint. Delay windows must be finite with `0 <= min <= max`; input and output windows can be negative but must be finite and ordered. At least one constrained output is required. Unconstrained internal branches are allowed and retain infinite required-time bounds.

For a node `v`, arrival propagation is:

```text
early[v] = min(early[u] + min_delay[u,v])
late[v]  = max(late[u]  + max_delay[u,v])
```

At an endpoint: `hold_slack = early - minimum_allowed`, and `setup_slack = maximum_allowed - late`. Negative slack means that modeled constraint is violated. Required-time propagation uses the corresponding reverse max/min recurrences. It does not add clock uncertainty, skew, latch borrowing, or clock reconvergence correction.

## Use

Build using the [workspace instructions](../README.md), then:

```sh
./build/timingforge/timingforge timingforge/examples/reconvergent.tfg
./build/timingforge/timingforge timingforge/examples/reconvergent.tfg --eco timingforge/examples/resize.eco
./build/timingforge/timingforge timingforge/examples/reconvergent.tfg --corners 0.8 1.0 1.2 --workers 3
```

The sample reports setup slack **0.5 ns** and hold slack approximately **-0.2 ns**. Its late path is `data_b → mux → alu → result`. The provided ECO changes arc 2 from `[0.5, 1.2]` to `[0.3, 0.8]`; setup slack becomes approximately **0.9 ns**, and the hold violation remains.

An ECO file contains `set ARC_ID MIN MAX`. The CLI applies all edits as one batch. Duplicate arc IDs are rejected. Edits specify absolute delay values, not scale factors. Corner analysis scales arc delays only, not constraints or input arrivals. ECO and corner mode cannot be combined in a single CLI invocation.

## C++ API

```cpp
auto graph = timingforge::parse(netlist_stream);
timingforge::Engine engine(graph);
auto baseline = engine.analyze();
std::vector<timingforge::Update> edits{{2, {0.3, 0.8}}};
auto revised = engine.update(edits);
```

`Graph` is immutable after construction. `Engine` is a value-owning mutable analysis object; synchronize access externally if you share one engine. Use `analyze_corners` to get independent, safely parallel analyses. `std::span` parameters borrow their input only for the call. Returned `Result` objects own their path and endpoint data.

## Complexity and limits

Graph assembly and a full arrival/required pass are `O(V+E)`, ignoring hash-table worst cases. Path materialization additionally costs the total number of nodes in returned paths. The ECO forward worklist takes `O(E_affected + V_affected log V_affected)`; the current implementation also copies `O(V+E)` state for rollback and recomputes all required times. **It is not a sublinear end-to-end ECO algorithm.** The visited-node counter measures forward recomputation only.

Floating-point equality governs change detection and ties. There is no epsilon pruning; all finite changed values propagate. Near-equal physical timings are not statistically equivalent simply because their outputs look rounded. Very large path sums are rejected on overflow.

Not implemented: rise/fall transition modeling, slew/load-dependent cell tables, path exceptions, multiple clocks, variation correlations, parasitics, timing-driven optimization, or path sensitization. Extensions should be accompanied by independent reference tests.

Conceptual domain reference: [OpenSTA's documented timing-engine features](https://openroad.readthedocs.io/en/latest/main/src/sta/README.html). No OpenSTA implementation code is incorporated.
