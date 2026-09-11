# Interview walkthrough

Read, build, and modify these projects before presenting them as evidence of your own understanding. They were created with AI-assisted implementation. Treat the repository as material to own technically; the tests cannot establish your personal proficiency.

## Five-minute TimingForge demo

1. Run the reconvergent sample and explain why latest arrival chooses `data_b → mux → alu → result` while earliest arrival takes `data_a → alu → result`.
2. Apply the ECO. Explain why setup improves but hold does not.
3. Walk through `Graph::Graph`, `Engine::recompute`, `Engine::update`, and `analyze_corners`.
4. Explain why a decrease in one delay requires examining all incoming edges. Show the test that switches the critical predecessor.
5. Be explicit: forward recomputation is selective, but rollback snapshots and the reverse pass are still linear. Do not describe the entire ECO operation as sublinear.

Questions to be ready for:

- What invalidates a `std::span`? Why can the CSR spans remain valid here?
- Why does the graph use `shared_ptr<const Graph>` rather than raw pointers or one graph copy per corner?
- What exactly does `memory_order_relaxed` guarantee for job assignment? Which operation synchronizes result visibility?
- Can writing different vector elements race? What changes if the vector resizes or stores `vector<bool>`?
- How is a tie in arrival times resolved? What happens when the path changes but the time does not?
- What is the difference between input validation, the basic exception guarantee, and a transactional update?
- Which real timing effects are absent from this model?

## Five-minute LogicScope demo

1. Run the registered adder and show its SVG/VCD waveform.
2. Show how a two-nanosecond pulse is rejected by a five-nanosecond buffer.
3. Explain the `(time, delta, sequence)` ordering and same-time batching with the XOR regression test.
4. Walk through generation-based cancellation and explain its queue-memory cost.
5. Trigger the zero-delay NAND oscillator and show the explicit budget failure.

Questions to be ready for:

- Why does `0 AND X` evaluate to zero? Why do `1` and `Z` resolve to one?
- Why is a priority queue's comparator reversed to implement a min-heap?
- What should a simulator do if data and clock change at exactly the same time?
- Why is this simulator single-threaded? What would safe parallel discrete-event simulation require?
- Why doesn't VCD retain delta as its own time coordinate?
- What are the memory and determinism tradeoffs of a full trace versus streaming output?

## Exercises that demonstrate ownership

1. Add a new timing constraint test before extending the parser; specify negative-slack behavior first.
2. Add a resettable DFF with explicit reset priority and tests for simultaneous reset and clock changes.
3. Add a trace sink that writes transitions incrementally. Define behavior for disk errors.
4. Optimize ECO rollback with a change log and compare both implementations against the existing oracle. Measure the effect honestly.
5. Add a new synthetic graph topology and compare its locality and scaling against the chain/skip-arc benchmark.

On a resume, use the recorded test counts or measured workload sizes. Do not invent production adoption, commercial-tool speedups, or Synopsys ownership of these personal projects.
