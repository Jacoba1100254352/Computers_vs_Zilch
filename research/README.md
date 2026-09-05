# Strategy research harness

`zilch_research` runs the actual `GameManager`, `Checker`, `ComputerController`,
turn loop, and Final Chase resolution. It does not approximate scoring or copy
the game into a second simulation engine. Build the ordinary CMake project in a
temporary directory outside the Dropbox checkout; it also creates this target.

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
