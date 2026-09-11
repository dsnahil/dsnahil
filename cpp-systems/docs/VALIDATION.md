# Validation record

Local validation on 2026-09-10:

- GCC 13.3, C++20, CMake 4.4.3, Ninja 1.13.2, Python 3.12.
- Release compilation succeeded with `-Wall -Wextra -Wpedantic -Werror`.
- All three CTest suites passed: TimingForge unit groups, LogicScope unit groups, and independent Python CLI integration.
- Debug compilation with AddressSanitizer and UndefinedBehaviorSanitizer succeeded; all three suites passed with `ASAN_OPTIONS=detect_leaks=0`.
- LeakSanitizer could not inspect processes in this container (ptrace/process-access restriction). Leak detection was disabled only for the local test command. The GitHub sanitizer job keeps normal settings. **No local leak-detection pass is claimed.**
- Both documented examples executed; TimingForge baseline/ECO values and LogicScope registered-adder behavior matched the READMEs.
- Five Release benchmark samples and one excluded warm-up were recorded.

GitHub CI additionally configures GCC Release, Clang Release, Clang ASan/UBSan, and a Docker build/test job. CI outcomes are reported by GitHub, separately from the above local results. A workflow file is not proof that those jobs completed.

Not measured or certified: statement/branch coverage, TSan execution, worst-case performance, platform portability beyond executed builds, HDL language compliance, or production readiness.
