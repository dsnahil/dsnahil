# Resume project descriptions

Personal projects, September 2026. Use these after running the demos and reviewing the implementation and tradeoffs. They describe this repository's implemented capabilities; they do not imply commercial deployment or work completed at Synopsys.

## TimingForge — C++20 Static Timing Analysis Engine

**C++20 · STL · Multithreading · CMake · Python · GitHub Actions**

- Built a C++20 timing-analysis engine using compressed sparse row graphs, topological propagation, and deterministic critical-path reconstruction to calculate earliest/latest arrivals and setup/hold slack; supported batched delay updates and concurrent analysis across delay scales.
- Validated timing results against exhaustive path enumeration on 120 randomized DAGs and verified update/rebuild equivalence; benchmarked a 50,000-node, 149,961-arc synthetic graph with reproducible measurement scripts.

## LogicScope — C++20 Event-Driven Digital Logic Simulator

**C++20 · STL · Python · VCD · Docker · CTest**

- Implemented a deterministic event-driven simulator with four-state logic, generation-based inertial event cancellation, delta-cycle scheduling, D flip-flops, tri-state resolution, and JSON/VCD waveform export.
- Verified 384 four-state gate combinations and 320 arithmetic test vectors across 2–16-bit ripple-carry adders; processed 844,800 events on an 8,192-gate synthetic benchmark and validated exported VCD traces against simulation results.

Optional performance detail, only with the benchmark context: the recorded GCC 13.3 Release run achieved a five-run median of approximately 8.44 million events/second on the documented buffer-chain workload, excluding parsing and waveform serialization. Do not call this a general simulator throughput guarantee.
