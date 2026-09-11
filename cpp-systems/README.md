# C++ Systems: TimingForge & LogicScope

Two independent C++20 libraries and command-line tools for exploring electronic-design software internals. Authored as personal portfolio projects with AI-assisted implementation and explicit correctness checks. No Synopsys source code, internal data, or proprietary benchmarks are included.

| Project | Engineering focus | Supporting stack |
| --- | --- | --- |
| [TimingForge](timingforge/) | CSR timing graph, earliest/latest propagation, critical paths, ECO updates, parallel delay corners | Python path oracle, JSON, CMake, CTest |
| [LogicScope](logicscope/) | Four-state logic, deterministic event batches, inertial delay, sequential elements, waveform export | Python arithmetic oracle, VCD, SVG waveform renderer |

Both compile with strict warnings and ship sanitizer configurations, a Docker recipe, and [GitHub Actions](../.github/workflows/cpp-systems.yml). The native libraries depend only on the C++ standard library. Python 3.10+ is used for reference tests and reporting.

**Maturity:** tested portfolio implementations of deliberately bounded models. They are not chip signoff tools, HDL compilers, or production-qualified replacements for commercial EDA products. See each project's limitations before interpreting results.

## Build and run

Requirements: C++20 compiler (tested locally with GCC 13.3), CMake 3.24+, Python 3.10+, and Ninja or Make. Run from this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
ctest --test-dir build --output-on-failure

./build/timingforge/timingforge timingforge/examples/reconvergent.tfg
./build/timingforge/timingforge timingforge/examples/reconvergent.tfg --eco timingforge/examples/resize.eco
./build/timingforge/timingforge timingforge/examples/reconvergent.tfg --corners 0.8 1.0 1.2 --workers 3

mkdir -p out
./build/logicscope/logicscope logicscope/examples/registered_adder.lsc logicscope/examples/registered_adder.stim --until 40 --vcd out/adder.vcd > out/adder.json
python3 scripts/render_waveform.py out/adder.json --output out/adder.svg
```

The VCD can be opened in a VCD-capable waveform viewer such as GTKWave. The SVG is viewable in a browser without a service or JavaScript.

## Verification

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-asan -j 2
ctest --test-dir build-asan --output-on-failure
```

The optional `-DENABLE_TSAN=ON` configures a separate ThreadSanitizer build; it must not be combined with ASan. TSan is provided as a configuration, not claimed as a locally completed check.

- 18 TimingForge native test groups: constraints, path changes, batched update equivalence, deterministic ties, worker failures, cycle rejection, and overflow rollback.
- 17 LogicScope native test groups: unknown/high-impedance values, resolution, pulse filtering, clock edges, simultaneous events, limits, replay, JSON, and VCD.
- 120 seeded randomized timing DAGs checked against **exhaustive path enumeration**, followed by ECO-versus-rebuild comparisons.
- 384 exhaustive three-input four-state truth-table comparisons.
- 320 arithmetic-oracle vectors across 2-, 4-, 8-, and 16-bit ripple-carry adders, with independent VCD replay checks.

See [VALIDATION.md](docs/VALIDATION.md) for execution evidence and environment limits. Tests are meaningful examples and differential checks; they do not establish exhaustive correctness or a coverage percentage.

## Reproduce measurements

```sh
python3 scripts/benchmark.py ./build/systems_bench --nodes 50000 --runs 5 --output out/benchmark.json
```

[Raw measurements](benchmarks/results/local-gcc13-release.json) include the compiler, environment, warm-up, samples, and medians. Timings are native in-process wall time; parsing and JSON serialization are excluded, allocation of results is included. The synthetic workload is documented in [benchmarks/README.md](benchmarks/README.md). No cross-machine or commercial-tool speed claims are implied.

## Container

```sh
docker build -t cpp-systems .
docker run --rm -v "$PWD:/data:ro" cpp-systems timingforge timingforge/examples/reconvergent.tfg
docker run --rm -v "$PWD:/data:ro" cpp-systems logicscope logicscope/examples/registered_adder.lsc logicscope/examples/registered_adder.stim --until 40
```

The build stage executes the test suite; the runtime uses an unprivileged user. To write VCD, mount a separate writable output directory owned by UID 10001. Docker validation is tracked separately from native validation.

## Read the engineering decisions

- [Architecture and tradeoffs](docs/ARCHITECTURE.md)
- [Interview walkthrough and exercises](docs/INTERVIEW.md)
- [Resume descriptions](docs/RESUME.md)
- [License](LICENSE)
