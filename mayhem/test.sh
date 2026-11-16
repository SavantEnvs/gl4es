#!/usr/bin/env bash
#
# mayhem/test.sh — run gl4es' known-answer self-test (built by mayhem/build.sh, normal flags).
#
# Upstream's tests (tests/tests.sh) replay apitrace captures against a real X11/GLES display and
# compare screenshots, which a container cannot run. /mayhem/gl4es_selftest
# (mayhem/harnesses/selftest.c) drives gl4es through its public GL API on the fake GLES driver
# and asserts what gl4es hands the driver: the GLSL ES it generates from desktop GLSL and ARB
# programs, the ARB error report, and the pixels from the DXT decoder and pixel converters.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

EXPECTED=12
RUNNER=/mayhem/gl4es_selftest
if [ ! -x "$RUNNER" ]; then
  echo "missing $RUNNER (mayhem/build.sh should have built it)" >&2
  emit_ctrf "gl4es-selftest" 0 "$EXPECTED" 0
  exit 1
fi
out=$("$RUNNER" 2>&1); rc=$?
printf '%s\n' "$out"
passed=$(printf '%s\n' "$out" | grep -c '^ok [0-9]')
failed=$(printf '%s\n' "$out" | grep -c '^not ok [0-9]')
# A run that stops early (crash, exit) reports fewer checks: count the missing ones as failed.
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -gt 0 ] && failed=$(( failed + missing ))
printf '%s\n' "$out" | grep -q "^summary passed=$passed failed=" || { [ "$failed" -gt 0 ] || failed=1; }
[ "$rc" -eq 0 ] || { [ "$failed" -gt 0 ] || failed=1; }
emit_ctrf "gl4es-selftest" "$passed" "$failed" 0
