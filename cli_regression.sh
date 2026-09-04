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
contains "$output" "--opening-score"
contains "$output" "--stealing BOOL"
contains "$output" "--difficulty LEVEL"
contains "$output" "--bot-a LEVEL"

output="$(expect_success "$BIN" arena --games 2 --threads 1 --seed 1 --score-limit 1000)"
contains "$output" "Policy A win rate: 0.500"
contains "$output" "avg margin=0.0"

output="$(expect_success "$BIN" arena --games 2 --threads 1 --seed 2 --score-limit 1000 \
    --opening-score 0 --straight off --three-pairs false --multiples on --singles true \
    --first-roll-bust disabled --final-chase no --allow-ties 0 --stealing enabled)"
contains "$output" "Policy A win rate: 0.500"

output="$(expect_success "$BIN" arena --bot-a hard --bot-b medium --games 2 --threads 1 \
    --seed 41 --score-limit 1000 --opening-score 0)"
contains "$output" "Bot A difficulty: Hard"
contains "$output" "Bot B difficulty: Medium"
contains "$output" "thresholds=200,1021,1128,1506,2130,2130"

output="$(expect_success "$BIN" arena --bot-a hard --bot-b medium --games 2 --threads 1 \
    --seed 42 --score-limit 1000 --opening-score 0 --stealing on)"
contains "$output" "thresholds=313,313,1106,1360,1360,1376"

expect_failure "Value out of range for --games" "$BIN" arena --games 1 --threads 1 --seed 1 --score-limit 1000
expect_failure "Invalid numeric value for --games" "$BIN" arena --games -2 --threads 1 --seed 1 --score-limit 1000
expect_failure "Unknown option: --bogus" "$BIN" arena --bogus true
expect_failure "Value out of range for --score-limit" "$BIN" play --score-limit 999
expect_failure "Value out of range for --opening-score" "$BIN" arena --games 2 --score-limit 1000 --opening-score 1001
expect_failure "Value out of range for --opening-score" "$BIN" play --score-limit 1000 --opening-score 1001
expect_failure "Invalid boolean value for --stealing" "$BIN" arena --games 2 --stealing maybe
expect_failure "Invalid boolean value for --final-chase" "$BIN" train --final-chase maybe
expect_failure "Invalid computer difficulty for --difficulty" "$BIN" play --difficulty expert
expect_failure "Invalid computer difficulty for --bot-a" "$BIN" arena --bot-a expert --games 2
expect_failure "--policy cannot be combined with --difficulty" "$BIN" play --policy "$POLICY" --difficulty hard
expect_failure "--policy-a cannot be combined with --bot-a" "$BIN" arena --policy-a "$POLICY" --bot-a hard --games 2
expect_failure "At least one scoring rule must be enabled" "$BIN" arena --games 2 \
    --straight off --three-pairs off --multiples off --singles off

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
output="$(expect_success "$BIN" train --generations 1 --population 4 --matches 8 --threads 2 \
    --score-limit 1000 --opening-score 0 --stealing on --allow-ties true \
    --output "$trained_policy" --seed 11)"
contains "$output" "Saved best policy to"
test -s "$trained_policy" || fail "expected train command to write a policy"
rm -f "$trained_policy"

output="$(printf '' | "$BIN" play --seed 5 --score-limit 1000 --opening-score 0 \
    --straight true --three-pairs on --multiples yes --singles enabled \
    --first-roll-bust true --final-chase on --allow-ties yes --stealing off \
    --policy "$POLICY" 2>&1)" || fail "play should exit cleanly on closed input"
contains "$output" "Input stream closed. Exiting Zilch."

output="$(printf '' | "$BIN" play --difficulty easy --seed 5 --score-limit 1000 --opening-score 0 2>&1)" || \
    fail "named Easy play should exit cleanly on closed input"
contains "$output" "Using Easy computer preset"
contains "$output" "thresholds=600,600,600,600,600,600"

echo "CLI regression checks passed."
