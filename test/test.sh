#!/usr/bin/env bash
#
# Run the pl0c test suite.
#
#   pass/*.pl0  must compile successfully (exit status 0).
#   fail/*.pl0  must be rejected (non-zero exit status). If the first line is
#               a comment of the form  { expect: some text }  the compiler's
#               error output must also contain "some text".
#
# Usage: ./test.sh [-v]      -v shows the compiler's output for failed tests

set -u

cd "$(dirname "$0")" || exit 1

COMPILER=../pl0c
SOURCE=../compiler.cc
TIMEOUT=5    # seconds; catches a compiler stuck in an infinite loop

verbose=0
[ "${1:-}" = "-v" ] && verbose=1

# Colours only when printing to a terminal.
if [ -t 1 ]; then
    GREEN=$'\e[32m' RED=$'\e[31m' BOLD=$'\e[1m' RESET=$'\e[0m'
else
    GREEN='' RED='' BOLD='' RESET=''
fi

# (Re)build the compiler if it's missing or older than its source.
if [ ! -x "$COMPILER" ] || [ "$SOURCE" -nt "$COMPILER" ]; then
    echo "Building $COMPILER..."
    g++ -std=c++17 -Wall -Wextra -o "$COMPILER" "$SOURCE" || {
        echo "${RED}build failed${RESET}"
        exit 1
    }
fi

passed=0
failed=0
failures=()

report() {    # report <name> <ok:0|1> <reason> <output>
    if [ "$2" -eq 0 ]; then
        passed=$((passed + 1))
        echo "  ${GREEN}PASS${RESET}  $1"
    else
        failed=$((failed + 1))
        failures+=("$1")
        echo "  ${RED}FAIL${RESET}  $1  ($3)"
        if [ "$verbose" -eq 1 ] && [ -n "$4" ]; then
            printf '%s\n' "$4" | sed 's/^/          | /'
        fi
    fi
}

echo "${BOLD}Programs that should compile${RESET}"
for t in pass/*.pl0; do
    out=$(timeout "$TIMEOUT" "$COMPILER" "$t" 2>&1)
    status=$?
    if [ "$status" -eq 124 ]; then
        report "$t" 1 "timed out after ${TIMEOUT}s" "$out"
    elif [ "$status" -ne 0 ]; then
        report "$t" 1 "exit status $status" "$out"
    else
        report "$t" 0 "" ""
    fi
done

echo
echo "${BOLD}Programs that should be rejected${RESET}"
for t in fail/*.pl0; do
    # Pull "some text" out of a first line like:  { expect: some text }
    expect=$(sed -n '1s/^{ *expect: *\(.*[^ ]\)\{0,1\} *}$/\1/p' "$t")
    out=$(timeout "$TIMEOUT" "$COMPILER" "$t" 2>&1)
    status=$?
    if [ "$status" -eq 124 ]; then
        report "$t" 1 "timed out after ${TIMEOUT}s" "$out"
    elif [ "$status" -eq 0 ]; then
        report "$t" 1 "compiled, but should have been rejected" "$out"
    elif [ "$status" -gt 128 ]; then
        report "$t" 1 "crashed with signal $((status - 128))" "$out"
    elif [ -n "$expect" ] && [[ "$out" != *"$expect"* ]]; then
        report "$t" 1 "expected error containing '$expect'" "$out"
    else
        report "$t" 0 "" ""
    fi
done

echo
total=$((passed + failed))
if [ "$failed" -eq 0 ]; then
    echo "${GREEN}${BOLD}All $total tests passed.${RESET}"
    exit 0
fi

echo "${RED}${BOLD}$failed of $total tests failed:${RESET}"
printf '  %s\n' "${failures[@]}"
exit 1
