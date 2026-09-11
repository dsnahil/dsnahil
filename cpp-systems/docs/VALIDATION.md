# Validation record

Local validation on 2026-09-10:

- GCC 13.3, C++20, CMake 4.4.3, Ninja 1.13.2, Python 3.12.
- Release compilation succeeded with `-Wall -Wextra -Wpedantic -Werror`.
- All three CTest suites passed: TimingForge unit groups, LogicScope unit groups, and independent Python CLI integration.
- Debug compilation with AddressSanitizer and UndefinedBehaviorSanitizer succeeded; all three suites passed with `ASAN_OPTIONS=detect_leaks=0`.
- LeakSanitizer could not inspect processes in this container (ptrace/process-access restriction). Leak detection was disabled only for the local test command. The GitHub sanitizer job keeps normal settings. **No local leak-detection pass is claimed.**
- Both documented examples executed; TimingForge baseline/ECO values and LogicScope registered-adder behavior matched the READMEs.
- Five Release benchmark samples and one excluded warm-up were recorded.

GitHub CI completed successfully on 2026-09-11 for commit `b033d09bea429dde3b93c01f80f0141d4b7dc336`: GCC Release, Clang Release, Clang AddressSanitizer/UndefinedBehaviorSanitizer with normal leak-detection settings, and the Docker build/test plus runtime smoke checks all passed. [Inspect the completed run](https://github.com/dsnahil/dsnahil/actions/runs/34641962665). These are executed CI results, separate from the local container's leak-detection limitation.

Not measured or certified: statement/branch coverage, TSan execution, worst-case performance, platform portability beyond executed builds, HDL language compliance, or production readiness.
