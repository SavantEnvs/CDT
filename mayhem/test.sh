#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
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

# Upstream's Catch2 suite (built by build.sh): ground-truth tests diff the triangulation of every
# CDT/tests/inputs/*.txt against CDT/tests/expected/*, plus predicate and API unit tests.
RUNNER="$SRC/build/tests/CDT-tests"
if [ ! -x "$RUNNER" ]; then
  echo "test runner $RUNNER missing — mayhem/build.sh must build it" >&2
  emit_ctrf "catch2" 0 1 0
  exit 1
fi
REPORT="$(mktemp "${TMPDIR:-/tmp}/cdt-junit.XXXXXX.xml")"
trap 'rm -f "$REPORT"' EXIT
# tests read inputs/ and expected/ relative to the working directory
(cd "$SRC/CDT/tests" && "$RUNNER" --reporter "JUnit::out=$REPORT" --reporter console::out=-) || true

# The JUnit report's <testsuite tests=".." failures=".." errors=".." skipped=".."> carries the counts;
# a runner that produced no report (crashed / no-op) counts as one failure.
read -r tests failures errors skipped < <(python3 - "$REPORT" <<'PY' 2>/dev/null || echo "0 1 0 0"
import sys, xml.etree.ElementTree as ET
r = ET.parse(sys.argv[1]).getroot()
s = r if r.tag == "testsuite" else r.find("testsuite")
print(int(s.get("tests", 0)), int(s.get("failures", 0)), int(s.get("errors", 0)), int(s.get("skipped", 0)))
PY
)
failed=$(( failures + errors ))
passed=$(( tests - failed - skipped ))
if [ "$tests" -eq 0 ]; then failed=1; passed=0; fi
emit_ctrf "catch2" "$passed" "$failed" "$skipped"
