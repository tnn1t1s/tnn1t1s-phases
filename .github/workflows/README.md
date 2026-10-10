# workflows

- `stress.yml` — manual-only (`workflow_dispatch`) pre-release gate: the
  sanitized DSP stress harness, cppcheck and clang-tidy. Run it with
  `gh workflow run stress.yml`. It is not a per-PR check.
