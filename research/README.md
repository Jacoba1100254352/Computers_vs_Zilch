# Strategy research harness

`zilch_research` runs the actual `GameManager`, `Checker`, `ComputerController`,
turn loop, and Final Chase resolution. It does not approximate scoring or copy
the game into a second simulation engine. Build the ordinary CMake project in a
temporary directory outside the Dropbox checkout; it also creates this target.

## Fixed-roll scoring-selection checkpoints

```sh
zilch_research --mode selection --roll 6,6,6,5,2,3 \
  --select-left 6,6,6,5 --action-left roll \
  --select-right 6,6,6 --action-right roll \
  --at-risk 0 --banked-a 0 --banked-b 0 \
  --collect-a true --collect-b true \
  --pairs 100000 --threads 8 --seed 2026090503 --output selection.json
```

For each independent seed, both treatments start with **the same already-rolled
dice and the same score at risk before selecting anything**. The left and right
face lists must each match an exact legal sequence of the real `Checker` scoring
options. The example compares taking the triple and five for 650 then rolling
two dice, versus taking only the triple for 600 then rolling three dice. Both
branches retain the six chain. Each forced `roll` resumes the real turn loop
without giving the bot an extra opportunity to select the discarded singleton
first. Each forced `bank` ends the selected turn immediately and is **rejected**
if the opening minimum is not met. No invalid Bank is silently converted to Roll.
After that single forced decision, the named A and B policies finish both full
matches. Policy A always controls acting player A, not the left treatment;
policy B controls the opponent in both treatments.

`--at-risk` defaults to zero in selection mode. `--roll` determines the dice
count, so `--dice` is rejected in this mode. Face lists support every multiple
face, three/four/five of a kind, either scoring singleton, hot dice, and other
legal complete scoring groups. Selecting only three of four identical dice is
rejected because it is not an option in the current real game engine.

For an existing chain, provide its six face-ordered scores, for example
`--roll 6,1,5 --saved-multiples 0,0,0,0,0,600 --at-risk 600`. Saved chains must have
legal powers-of-two scores, fit among the dice removed from the current six-die
set, and not exceed the score at risk. A full six-die roll cannot carry a chain
from before hot dice. A partial roll requires nonzero prior points. No new
extension-rule variant is invented: all continuations use the actual game's
existing multiple-extension behavior.

For the last turn of Final Chase, pass `--active-final-chase true`, keep A below
the target, and set B at or above it. For example, `--banked-a 3500 --banked-b 5500`
starts A's last turn chasing B; banking or busting resolves the match using the
normal final-round winner/tie logic. With this flag false (the default), both
banked scores must be below the target. The rule toggle `--final-chase on|off`
continues to control whether crossing the target starts a final round, separate
from whether one is already active at the checkpoint.

Selection JSON uses schema 2 and includes the full roll, pre-selection state,
left/right requested selections and actions, every applied scoring option,
post-selection points, remaining dice, saved-chain scores, bankability, and
Final Chase state. It also records the acting policy's deterministic incumbent
recommendation at the original checkpoint. `left` and `right` contain separate
raw match results. The paired fields are
`right_minus_left_match_points_paired` and
`right_minus_left_score_margin_paired`; positive values favor the right branch.
They include sums, sums of squares, standard errors, and 95% intervals. The fixed
roll is registered as already happened, so the next roll cannot receive the
first-roll-only mercy bonus. A synthetic count of one represents that invariant;
the checkpoint does not invent an exact transcript of preceding scoring rolls.

## Exact-state experiment

```sh
zilch_research --mode state --pairs 100000 --threads 8 --seed 2026090501 --output state.json
```

For each independent seed, branch the same bankable state into **bank now** and
**roll at least once**, then use the same continuation policies through the end
of both matches. Defaults are both banked scores zero, A acting in seat 0, 2,800
at risk, all six hot dice, no active Final Chase, and standard rules with stealing
off. This is a later roll in an already-scored turn: a bust on the next roll cannot
receive first-roll mercy. No saved multiple is carried into this hot-dice state.
`--at-risk`, `--banked-a`, `--banked-b`, and `--dice` support other pre-Final-Chase
states; partial-dice states also start without saved multiples.

The primary result is the paired difference in A's eventual match points
(roll minus bank), not the difference in the immediate roll's expected points.
The two branches share a seeded RNG stream, but diverging actions consume draws
at different times. This is a common-random-number comparison, not a promise
that both branches receive the same future dice for each corresponding player.

## Full-game comparisons

```sh
zilch_research --mode duel --policy-a candidate.cfg --difficulty-a hard \
  --difficulty-b hard --pairs 100000 --threads 8 --seed 2026090502 --output duel.json
```

Each pair plays A vs B and then B vs A with the same random seed. Unlike the
historical trainer's raw policies, this harness can combine a policy file with
the actual Hard difficulty behavior, including its endgame layer. Difficulty
defaults to Hard; `raw` disables the difficulty-specific layer and uses the raw
baseline if no policy file is supplied. `easy` and `medium` provide additional
opponents. `--collect-a true` / `--collect-b true` independently enable the
experimental collect-remaining-scoring-dice-before-banking behavior, off by
default. Full effective policy parameters and feature flags are in every JSON
result, so file paths alone are not the policy identifier.

Current released Hard includes collection before banking, so explicitly pass
`--collect-a true --collect-b true` when testing that behavior. The research
executable intentionally retains its old false defaults for reproducibility.
Research-only policy controls are `--chain-risk-a N` / `--chain-risk-b N` (weights
from 0 through 8, default 0), `--chain-mode-a raise|blend` /
`--chain-mode-b raise|blend` (default raise), and `--safe-finish-a true|false` /
`--safe-finish-b true|false` (default false). They are passed independently to the
actual controllers and recorded in policy metadata. Their implementation applies
only to named Hard with Stealing off. Defaults do not change the old state or
duel treatment behavior; additional metadata states that these features are off.

Rule options include `--target`, `--opening-score`, `--sets`, `--stealing`,
`--final-chase`, `--first-roll-mercy`, and `--ties`. Boolean values accept `true`
or `false`, and `on` or `off`. `--games` is an even-total-games alias for `--pairs`.

## Reproducibility and interpretation

The explicit master seed initializes `mt19937_64`. Each output supplies both
32-bit halves to `seed_seq`, which initializes that pair's `mt19937` dice RNG.
Results do not depend on worker scheduling. The old CLI's seeded behavior is
unchanged; it historically truncates each match seed to 32 bits and therefore
does not generate the same dice stream as this research executable.

Wins, ties, losses, score totals, full configuration, and seed procedure are
recorded. Confidence intervals use the sample variance of **independent pairs**:
the mean of A's two seat outcomes for duels, or the per-seed roll-minus-bank
treatment difference for exact-state experiments. Thus the paired games are
not incorrectly counted as independent Bernoulli trials. One pair has no
estimable standard error and emits null intervals. Reported 95% intervals use a
normal approximation and do not correct for searching multiple candidates.
Use exploratory seeds for tuning and fresh, prespecified holdout seeds for a
final comparison. Pairing controls seat effects but does not demonstrate optimal
play or generalize a result to untested opponents, rules, or score limits.

`zilch_research_tests` verifies that forced banking preserves the 2,800-point
state, resuming does not reset the turn, later-roll busts do not receive mercy,
Final Chase remains active when applicable, and identical mirrored policies
produce exactly balanced results.
It also covers exact legal fixed selections, all triple faces, larger multiples,
saved-chain extensions, hot-dice reset, rejection of unbankable Bank and invalid
states, and immediate match resolution during active Final Chase.
`zilch_selection_cli_tests` verifies parsing/metadata, identical-branch zero
effects, bit-identical results across worker counts, use of both halves of the
64-bit pair seed, and exclusive no-overwrite evidence writes.
