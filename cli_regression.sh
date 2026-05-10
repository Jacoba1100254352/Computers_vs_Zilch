#!/bin/sh
set -eu

BIN="$1"
ROOT="$2"
POLICY="$ROOT/trained_policy.cfg"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

contains() {
    haystack="$1"
    needle="$2"
    case "$haystack" in
        *"$needle"*) ;;
        *) fail "expected output to contain: $needle
Actual output:
$haystack" ;;
    esac
}

expect_success() {
    output="$("$@" 2>&1)" || fail "expected success: $*
$output"
    printf '%s' "$output"
}

expect_failure() {
    expected="$1"
    shift
    if output="$("$@" 2>&1)"; then
        fail "expected failure: $*
$output"
    fi
    contains "$output" "$expected"
}

output="$(expect_success "$BIN" --help)"
contains "$output" "Usage:"

output="$(expect_success "$BIN" arena --games 2 --threads 1 --seed 1 --score-limit 1000)"
contains "$output" "Policy A win rate: 0.500"
contains "$output" "avg margin=0.0"

expect_failure "Value out of range for --games" "$BIN" arena --games 1 --threads 1 --seed 1 --score-limit 1000
expect_failure "Invalid numeric value for --games" "$BIN" arena --games -2 --threads 1 --seed 1 --score-limit 1000
expect_failure "Unknown option: --bogus" "$BIN" arena --bogus true
expect_failure "Value out of range for --score-limit" "$BIN" play --score-limit 999

blank_policy="$(mktemp "${TMPDIR:-/tmp}/zilch_blank_policy.XXXXXX")"
: > "$blank_policy"
expect_failure "Failed to load policy" "$BIN" arena --policy-a "$blank_policy" --games 2 --threads 1 --seed 1 --score-limit 1000
rm -f "$blank_policy"

bad_policy="$(mktemp "${TMPDIR:-/tmp}/zilch_bad_policy.XXXXXX")"
cat > "$bad_policy" <<'POLICY'
name=bad
bank_thresholds=350,500,700,850,1000,1150
not-a-policy-line
score_weight=1
remaining_dice_weight=55
hot_dice_weight=240
multiple_weight=95
lead_factor=0.08
trail_factor=0.10
closing_factor=0.25
roll_bias=15
POLICY
expect_failure "Failed to load policy" "$BIN" arena --policy-a "$bad_policy" --games 2 --threads 1 --seed 1 --score-limit 1000
rm -f "$bad_policy"

expect_failure "Failed to save policy" "$BIN" train --generations 1 --population 4 --matches 8 --threads 2 --score-limit 1000 --output "${TMPDIR:-/tmp}" --seed 3
expect_failure "Failed to load resume policy" "$BIN" train --generations 1 --population 4 --matches 8 --threads 2 --score-limit 1000 --resume "${TMPDIR:-/tmp}/zilch_missing_resume.cfg" --output "${TMPDIR:-/tmp}/zilch_unused_policy.cfg" --seed 3

trained_policy="$(mktemp "${TMPDIR:-/tmp}/zilch_trained_policy.XXXXXX")"
output="$(expect_success "$BIN" train --generations 1 --population 4 --matches 8 --threads 2 --score-limit 1000 --output "$trained_policy" --seed 11)"
contains "$output" "Saved best policy to"
test -s "$trained_policy" || fail "expected train command to write a policy"
rm -f "$trained_policy"

output="$(printf '' | "$BIN" play --seed 5 --score-limit 1000 --policy "$POLICY" 2>&1)" || fail "play should exit cleanly on closed input"
contains "$output" "Input stream closed. Exiting Zilch."

echo "CLI regression checks passed."
