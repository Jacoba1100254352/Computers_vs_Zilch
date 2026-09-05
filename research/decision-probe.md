# Cross-implementation decision probe

`zilch_decision_probe --policy FILE --collect true|false` reads states from
standard input and emits one JSON decision per input line. Both flags are
required. A file supplies the full policy; the controller always uses Hard's
actual endgame layer. The explicit collector flag permits either incumbent or
refined behavior, including a separate Stealing policy.

Each input line contains exactly these 12 whitespace-separated fields:

```text
turnScore ownScore opponentScore target opening stealing finalChaseEnabled finalChaseActive ties sets diceCSV chainScoresCSV
```

- Scores are integer values from 0 through 1,000,000. Target is at least 1,000;
  opening cannot exceed target.
- Flags accept `0`/`1` or `false`/`true`.
- `diceCSV` contains one through six rolled face values, for example `1,1,5,2,3,4`.
- `chainScoresCSV` contains six saved multiple scores in face order, with zero
  for no chain. This uses the C++ engine's native saved-score representation.
  For web `scoredMultiples[face] = count`, the adapter is
  `(face === 1 ? 1000 : face * 100) * 2 ** (count - 3)`.
- The actor is seat 0. Active Final Chase is initialized as having been started
  by the opponent; it requires Final Chase to be enabled.

The input represents a roll that has just landed, before selecting any dice.
Scoring and selection are performed entirely by `Checker` and
`ComputerController`. The probe never rolls dice, banks the resulting score, or
implements its own scoring rules. Multiple selections and committed banking
through hot dice follow the actual controller loop.

Example input:

```text
2700 0 0 5000 1000 0 1 0 1 1 1,1,5,2,3,4 0,0,0,0,0,0
```

For the refined standard policy with collection enabled, the output is:

```json
{"line":1,"selected_counts":[2,0,0,0,1,0],"action":"Bank","score_gain":250,"projected_turn_score":2950,"next_dice":3,"can_bank":true}
```

`selected_counts` is ordered by face, not original die position. `action` is
`Bank`, `Roll`, or `Bust`. Bust means the input roll has no legal scoring option;
the probe does not apply first-roll mercy or turn-transition behavior. Invalid
input fails with a line-numbered error and nonzero exit status.

Run `zilch_decision_probe --self-test` or the CTest
`zilch_decision_probe_smoke` test for collection, explicit collector-off,
2,800-point hot dice, opening, saved-chain, active Final Chase, Sets-off, bust,
and hot-dice bank-commitment checks.
