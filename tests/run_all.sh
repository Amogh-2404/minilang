#!/usr/bin/env bash
# tests/run_all.sh -- minilang test harness.
#
# Runs three test types:
#   1. unit tests (gtest)         -- if built
#   2. integration tests          -- FileCheck on -my-passes IR
#   3. e2e tests                  -- diff -jit stdout against .out fixtures
#
# Exits non-zero on the first failure. CI runs this; the developer should
# too, before pushing.

set -u
set -o pipefail

# Locate paths relative to this script.
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"

MINILANG="$BUILD/minilang"
UNIT_BIN="$BUILD/minilang_unit_tests"

red()   { printf '\033[0;31m%s\033[0m\n' "$*"; }
green() { printf '\033[0;32m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n'   "$*"; }

if [[ ! -x "$MINILANG" ]]; then
    red "minilang binary not found at $MINILANG"
    red "build first: cmake -G Ninja -B build && ninja -C build"
    exit 1
fi

FAIL=0

# ---------- 1. unit tests ----------
bold "[1/3] unit tests"
if [[ -x "$UNIT_BIN" ]]; then
    if "$UNIT_BIN"; then
        green "unit tests passed"
    else
        red "unit tests failed"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  (no unit binary -- GoogleTest not found at configure time, skipping)"
fi

# ---------- 2. integration tests (FileCheck) ----------
bold "[2/3] integration tests"

# Locate FileCheck. Brew's llvm@18 installs FileCheck under its keg.
if command -v FileCheck >/dev/null 2>&1; then
    FILECHECK="FileCheck"
elif [[ -x /opt/homebrew/opt/llvm@18/bin/FileCheck ]]; then
    FILECHECK="/opt/homebrew/opt/llvm@18/bin/FileCheck"
elif [[ -x /usr/lib/llvm-18/bin/FileCheck ]]; then
    FILECHECK="/usr/lib/llvm-18/bin/FileCheck"
else
    echo "  (FileCheck not found; skipping integration tests)"
    FILECHECK=""
fi

if [[ -n "$FILECHECK" ]]; then
    INT_PASS=0
    INT_FAIL=0
    for t in "$ROOT"/tests/integration/*.ml; do
        name=$(basename "$t")
        if "$MINILANG" -emit-llvm -my-passes < "$t" 2>/dev/null \
                | "$FILECHECK" "$t" >/dev/null 2>&1 ; then
            INT_PASS=$((INT_PASS + 1))
        else
            red "  FAIL: $name"
            "$MINILANG" -emit-llvm -my-passes < "$t" \
                | "$FILECHECK" "$t" || true
            INT_FAIL=$((INT_FAIL + 1))
        fi
    done
    if [[ $INT_FAIL -eq 0 ]]; then
        green "integration tests passed ($INT_PASS/$INT_PASS)"
    else
        red "integration tests: $INT_FAIL failed, $INT_PASS passed"
        FAIL=$((FAIL + 1))
    fi
fi

# ---------- 3. e2e tests ----------
bold "[3/3] e2e tests"
E2E_PASS=0
E2E_FAIL=0
for t in "$ROOT"/tests/e2e/*.ml; do
    expected="${t%.ml}.out"
    [[ -f "$expected" ]] || continue
    name=$(basename "$t")
    actual=$("$MINILANG" -jit < "$t" 2>/dev/null)
    if diff -q <(printf '%s\n' "$actual") "$expected" >/dev/null 2>&1; then
        E2E_PASS=$((E2E_PASS + 1))
    else
        red "  FAIL: $name"
        echo "    expected: $(cat "$expected")"
        echo "    actual:   $actual"
        E2E_FAIL=$((E2E_FAIL + 1))
    fi
done
if [[ $E2E_FAIL -eq 0 ]]; then
    green "e2e tests passed ($E2E_PASS/$E2E_PASS)"
else
    red "e2e tests: $E2E_FAIL failed, $E2E_PASS passed"
    FAIL=$((FAIL + 1))
fi

# ---------- summary ----------
echo
if [[ $FAIL -eq 0 ]]; then
    green "all tests passed"
    exit 0
else
    red "$FAIL test category/categories failed"
    exit 1
fi
