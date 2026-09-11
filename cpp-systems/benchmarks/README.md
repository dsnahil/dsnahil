# Benchmark methodology

The benchmark executable runs both native libraries in process. The Python driver performs one excluded warm-up and five measured invocations. Each invocation reconstructs the fixtures before the timed kernels. Compiler/environment details and every raw sample are retained in [the recorded result](results/local-gcc13-release.json).

TimingForge uses 50,000 nodes with incoming arcs at strides 1, 7, and 31: 149,961 total arcs. This deliberately reconvergent synthetic graph has one source and one constrained sink. It measures a full analysis, a single delay edit adjacent to the sink, and four delay-scale analyses with four workers. Full analysis is warmed on an existing engine; corner analysis includes engine construction. These numbers are **not** a fair serial/parallel speedup comparison. The local ECO visits one forward node but still includes whole-state snapshots, the full reverse pass, and path materialization.

LogicScope uses 256 independent chains of 32 buffer gates, with 100 input transitions on each chain. This is 8,192 gates and 844,800 processed events, with 25,600 initial queued stimulus events. Trace allocation is included; JSON/VCD serialization is not. This low-fan-in, zero-cancellation workload does not represent all circuit topologies or glitch-heavy designs.

Local GCC 13.3 Release medians from five runs:

| Metric | Result |
| --- | ---: |
| Full timing analysis | 1.761 ms |
| Delay update | 1.594 ms |
| Four corners / four workers | 7.273 ms |
| Logic simulation | 100.076 ms |
| Logic processed events / second | 8.44 million |

Shared-container scheduling, CPU allocation, caches, topology, and compiler flags affect results. The raw file is the source of truth. Use workload sizes and verified functionality in a resume unless you are comfortable reproducing and defending the measurement protocol.
