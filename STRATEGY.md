# Computer strategy and reproducibility

The named computer levels build on the same policy and rules engine used by
`play`, `arena`, and `train`. The two tracked Hard policy files are the canonical
inputs for the browser edition and future strategy comparisons.

The historical experiments below used `main` at
`17a4102b7f2688be010f47ce472a0707cf007e48`, before the named-level layer was
added. The named-level comparisons below used the parity implementation built
directly on that exact revision.

## Current multiple-aware Hard refinement

Named Hard with Stealing off now uses the frozen chain-scoped planner selected
by the multiple-selection study. It compares legal scoring selections and
Bank/Roll actions together when a multiple is available or a saved multiple
exists. Its full-strength symmetric blend uses the saved-chain next-roll
scoring expectation and bust probability, then retains the existing opening,
lead/trail, closing, and Final Chase adjustments. Collecting all scoring dice
for hot dice competes with keeping the chain; neither is always preferred.

The exact feature pack is `chainRiskWeight=1`, `lowerChainThresholds=true`,
`jointSelection=true`, `jointChainsOnly=true`, and
`safeFinishCollection=true`. The independent finish guard also collects an
available guaranteed outright win on non-chain rolls. Outside chain scope,
ordinary selection and collect-before-bank remain in use. The six-dice base
cutoff stays 5,000, and the policy-file coefficients are unchanged.

This is enabled by the ordinary named-Hard controller constructor. Explicit
`ResearchFeatures{}` keeps the preceding behavior, so research and ablation
callers cannot silently acquire the new default. Raw policy files, training,
Easy, Medium, and Stealing retain their existing behavior. See the
[multiple-selection evidence](https://github.com/anderson-webops/zilch.jacobdanderson.net/tree/main/docs/research/multiple-selection-2026-09)
for the checkpoint confirmations, frozen candidate, and full-game holdouts.
The evidence concerns tested two-player configurations, not mathematical
optimality or every custom-rule setting.

## Previous hot-dice refinement

The preceding standard Hard banked near `200,1050,1150,1550,2150,5000` for one through
six dice, subject to its opening and endgame rules. After its existing scoring
plan commits to banking, it collects remaining guaranteed scoring points and
retains that commitment if collecting produces hot dice. Stealing retains its
separate policy and does not enable this collector. Raw policy-file runs remain
parameter-only; named Hard enables both endgame and collection.

The frozen refinement earned 51.5992% match points against incumbent Hard in
500,000 fresh games (250,000 mirrored pairs), with a paired 95% interval of
51.5289% to 51.6695%. A separate collector-only comparison supports the six-dice
change itself. The reported 2,800-point hot-dice continuation improved eventual
match points by 2.505 percentage points in 250,000 paired branch experiments.

All 42 experiments and the frozen policy are retained in the
[web research record](https://github.com/anderson-webops/zilch.jacobdanderson.net/tree/main/docs/research/hot-dice-2026-09).
The experiments used clean simulator revision `5e27d7c`, fixed opponents,
full-precision threshold adjustments, and paired moments for uncertainty.
They support a targeted two-player non-Stealing refinement, not optimality or
every custom configuration. Larger four/five-dice thresholds did not improve
the selected candidate in tuning. See `research/README.md` for the new harness.

## Named levels

| Level | Scoring and banking behavior |
| --- | --- |
| Easy | Takes every available scoring option, normally banks at 600 round points, and still banks immediately when that completes a win. |
| Medium | Takes all available scoring dice, uses dice-aware base thresholds, and compares the leader, finish line, and remaining dice before staging below 5,000 or building a Final Chase buffer. |
| Hard | Standard play adds chain-aware joint selection and safe winning-score collection to the tested policy and finish awareness. Stealing retains its separately trained policy. |

The finish rules are deliberately readable heuristics. With Final Chase on, a
named Medium or Hard bot may stop within 150 points of the target when it has a
lead of at least 500. After crossing the target, it seeks a 500-point buffer when
the nearest opponent is within 1,000 of the target and a 1,000-point buffer when
that opponent is within 500. It abandons the buffer when too few dice remain.
During Final Chase it keeps rolling until it can tie, or strictly beat the leader
when ties are disabled.

Explicit `--policy` and `--policy-a`/`--policy-b` runs do not apply these named
level heuristics. This preserves the original parameter-only arena and training
behavior for reproducible research.

## Tracked Hard policies

### Standard rules

File: `trained_policy.cfg`

SHA-256: `aff692a2a5efba2777f9963754d026cb7750b2102071bd984f58e69c41c77abe`

Base banking thresholds by dice available for the next roll. Current standard
Hard adjusts these for saved multiples inside its planner; this is not a fixed
banking table for every chain state:

| Dice | Exact policy cutoff | Next attainable score |
| ---: | ---: | ---: |
| 1 | 200 | 200 |
| 2 | 1,021 | 1,050 |
| 3 | 1,128 | 1,150 |
| 4 | 1,506 | 1,550 |
| 5 | 2,130 | 2,150 |
| 6 | 5,000 | 5,000 |

The historical standard policy (six-dice cutoff 2,130, now archived at
`research/incumbent-standard-hard.cfg`, SHA-256
`0c0020a049773aca3f66fc6915d6509a79bd4a2f55bfe7a3f0a7fddbedd5713b`)
scored 58.5621% match points against the built-in baseline
in 500,000 two-player games, arranged as 250,000 same-seed mirrored pairs. It
recorded 291,002 wins, 3,617 ties, and an average margin of +266.0 with seed
`2026091101`. A rough 95% interval is 58.3690% to 58.7552% when mirrored pairs
are treated as the effective sample:

```bash
./build/zilch arena \
  --policy-a trained_policy.cfg \
  --games 500000 \
  --threads 6 \
  --seed 2026091101
```

### Stealing enabled

File: `trained_stealing_policy.cfg`

SHA-256: `5ed0b4d2da825826f4389cbc7d57b5e46f7a900beae0a97a9bd442bddb6d48c4`

The retained training command was:

```bash
./build/zilch train \
  --generations 100 \
  --population 48 \
  --matches 4800 \
  --threads 4 \
  --seed 2026090416 \
  --output trained_stealing_policy.cfg \
  --stealing on
```

Its base cutoffs are `313,313,1106,1360,1360,1376`, with practical banking
points of `350,350,1150,1400,1400,1400`. Its utility equation accepts a carried
Stealing score at approximately `550,450,350,250,150` points for one through
five dice respectively.

The policy scored 52.0527% match points against the built-in baseline in 500,000
two-player holdout games, with 258,435 wins, 3,657 ties, an average margin of
+63.4, seed `2026091102`, and a rough 95% interval of 51.8569% to 52.2485%:

```bash
./build/zilch arena \
  --policy-a trained_stealing_policy.cfg \
  --games 500000 \
  --threads 6 \
  --seed 2026091102 \
  --stealing on
```

## Historical Three Pairs comparisons

Disabling Three Pairs did not produce evidence for a different public policy.
The standard Hard policy earned 57.9815% against the baseline over 500,000 games
with seed `2026091103`, an average margin of +247.9, and a rough 95% interval of
57.7880% to 58.1750%. A separately trained Three-Pairs-off candidate earned only
48.4% against the tracked standard policy over 150,000 games.

With both Stealing enabled and Three Pairs disabled, the Stealing Hard policy
earned 51.7056% against the baseline over 500,000 games with seed `2026091104`,
an average margin of +48.1, and a rough 95% interval of 51.5097% to 51.9015%.
These results support using the same standard or Stealing policy with Three
Pairs on or off. They do not establish policy portability for disabling
straights, multiples, or singles.

The retained policy checks can be reproduced with:

```bash
./build/zilch arena --policy-a trained_policy.cfg \
  --games 500000 --threads 6 --seed 2026091103 --three-pairs off
./build/zilch arena --policy-a trained_stealing_policy.cfg \
  --games 500000 --threads 6 --seed 2026091104 \
  --stealing on --three-pairs off
```

## Historical named-level validation

The named presets were evaluated separately because their explicit finish
heuristics are intentionally outside the parameter-only policy files:

| Matchup | Rules | Games | Seed | Policy A match points | Rough 95% interval |
| --- | --- | ---: | ---: | ---: | ---: |
| Hard vs Medium | Standard | 500,000 | 2026091201 | 58.9193% | 58.7264% to 59.1122% |
| Hard vs raw standard policy | Standard | 500,000 | 2026091202 | 52.5140% | 52.3182% to 52.7098% |
| Medium vs Easy | Standard | 250,000 | 2026091203 | 56.4598% | 56.1849% to 56.7347% |
| Hard vs Medium | Stealing on | 500,000 | 2026091204 | 52.9779% | 52.7822% to 53.1736% |
| Hard vs raw Stealing policy | Stealing on | 500,000 | 2026091205 | 53.2835% | 53.0879% to 53.4791% |

The exact commands were:

```bash
./build/zilch arena --bot-a hard --bot-b medium \
  --games 500000 --threads 3 --seed 2026091201
./build/zilch arena --bot-a hard --policy-b trained_policy.cfg \
  --games 500000 --threads 3 --seed 2026091202
./build/zilch arena --bot-a medium --bot-b easy \
  --games 250000 --threads 3 --seed 2026091203
./build/zilch arena --bot-a hard --bot-b medium \
  --games 500000 --threads 3 --seed 2026091204 --stealing on
./build/zilch arena --bot-a hard --policy-b trained_stealing_policy.cfg \
  --games 500000 --threads 6 --seed 2026091205 --stealing on
```

Each arena comparison mirrors seat order with the same dice seed. Mirroring
reduces first-player bias but does not remove sampling noise. The rough intervals
use the number of mirrored pairs as the effective sample and a bounded-proportion
normal approximation. Per-pair outcomes were not retained, so these are not
paired-variance confidence intervals.

## Limits

- These results cover two-player games with a 5,000-point target, a 1,000-point opening requirement, and the documented default rules unless a row says Stealing is on.
- The trainer searches a fixed parameterized policy family. It does not prove a global mathematical optimum.
- Hot-dice cycles make turn scores theoretically unbounded, and Final Chase makes decisions depend on opponent state.
- The named finish heuristics have arena evidence above, but they were not part of the earlier training process. Keep parameter-only policy comparisons separate from named-level comparisons.
- The repository retains the exact standard policy and holdout command, but not the original standard training command or training seed.
