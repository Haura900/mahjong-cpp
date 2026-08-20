# Deep-search pruning ablation and profile

## Conditions

- Three documented 4-shanten hands; turn 6; `auto_disable_deep_search=false`.
- Configs: `000`, `100`, `010`, `001`, `111`; pruning semantics unchanged.
- `enable_turn_yaku=true`, so searched/profile counters aggregate the three internal overlay calculations.
- Wall time is one Release run per cell; use phase timings and graph sizes for attribution, not small cross-run timing differences.

## Result

**`100` is the primary EV-damaging rule. `001` prunes nothing in all three hands while adding calculator calls. `010` preserves win/tenpai probabilities but changes EV in two hands. `111` is not an adoption candidate.**

### `147m258p369s11122z`

| config | best discard | EV | win | tenpai | searched | wall ms | graph ms | CSR ms | DP ms | edges |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 000 | 9s | 281.9693 | 6.7000% | 21.3800% | 3,045,535 | 25563.0 | 11986.7 | 829.7 | 12706.0 | 11,097,654 |
| 100 | 1m | 139.3664 | 3.0900% | 11.9000% | 2,283,203 | 19290.4 | 9399.4 | 608.1 | 9250.6 | 7,875,137 |
| 010 | 9s | 281.9693 | 6.7000% | 21.3800% | 3,004,522 | 22871.2 | 11158.8 | 684.9 | 10987.8 | 10,874,975 |
| 001 | 9s | 281.9693 | 6.7000% | 21.3800% | 3,045,535 | 24810.4 | 12025.0 | 755.5 | 11988.4 | 11,097,654 |
| 111 | 1m | 139.3664 | 3.0900% | 11.9000% | 2,242,376 | 19702.3 | 8859.5 | 511.1 | 10303.3 | 7,654,038 |

### `133m188p469s12467z`

| config | best discard | EV | win | tenpai | searched | wall ms | graph ms | CSR ms | DP ms | edges |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 000 | 4s | 106.1777 | 0.6000% | 3.8400% | 1,443,133 | 9780.8 | 6448.2 | 307.6 | 3009.1 | 5,363,372 |
| 100 | 4s | 37.7921 | 0.4800% | 3.2700% | 1,348,476 | 9145.8 | 6100.5 | 274.0 | 2758.3 | 4,936,677 |
| 010 | 4s | 114.7817 | 0.6000% | 3.8400% | 1,213,781 | 8094.3 | 5430.8 | 259.8 | 2388.7 | 4,490,874 |
| 001 | 4s | 106.1777 | 0.6000% | 3.8400% | 1,443,133 | 10064.4 | 6617.4 | 321.5 | 3109.1 | 5,363,372 |
| 111 | 4s | 41.4368 | 0.4800% | 3.2700% | 1,141,140 | 7668.8 | 5215.5 | 234.5 | 2205.1 | 4,161,367 |

### `2359m23358p89s125z`

| config | best discard | EV | win | tenpai | searched | wall ms | graph ms | CSR ms | DP ms | edges |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 000 | 2z | 86.0893 | 2.9700% | 11.3600% | 3,886,491 | 31212.9 | 15214.5 | 900.0 | 15046.6 | 13,766,865 |
| 100 | 2z | 75.8701 | 2.5800% | 10.1900% | 3,737,556 | 25134.9 | 13948.0 | 760.5 | 10379.1 | 12,958,179 |
| 010 | 2z | 80.6721 | 2.9700% | 11.3600% | 1,912,825 | 12760.7 | 6742.7 | 366.5 | 5632.5 | 6,023,715 |
| 001 | 2z | 86.0893 | 2.9700% | 11.3600% | 3,886,491 | 27561.5 | 15330.3 | 851.3 | 11333.1 | 13,766,865 |
| 111 | 2z | 70.6062 | 2.5800% | 10.1900% | 1,804,601 | 12483.9 | 6638.7 | 341.7 | 5490.6 | 5,462,909 |

## Rule attribution

| hand | rule | searched change | best EV change | win change | tenpai change | rule fires | extra Unnecessary calls vs 000 |
|---|---|---:|---:|---:|---:|---:|---:|
| `147m258p369s11122z` | 100 | -25.0% | -142.6029 | -3.6100% | -9.4800% | 13 | -1,183,529 |
| `147m258p369s11122z` | 010 | -1.3% | +0.0000 | +0.0000% | +0.0000% | 3,969 | -84,446 |
| `147m258p369s11122z` | 001 | +0.0% | +0.0000 | +0.0000% | +0.0000% | 0 | +90,060 |
| `133m188p469s12467z` | 100 | -6.6% | -68.3856 | -0.1200% | -0.5700% | 13 | -155,707 |
| `133m188p469s12467z` | 010 | -15.9% | +8.6040 | +0.0000% | +0.0000% | 4,053 | -401,809 |
| `133m188p469s12467z` | 001 | +0.0% | +0.0000 | +0.0000% | +0.0000% | 0 | +91,720 |
| `2359m23358p89s125z` | 100 | -3.8% | -10.2192 | -0.3900% | -1.1700% | 10 | -250,404 |
| `2359m23358p89s125z` | 010 | -50.8% | -5.4172 | +0.0000% | +0.0000% | 2,205 | -3,916,673 |
| `2359m23358p89s125z` | 001 | +0.0% | +0.0000 | +0.0000% | +0.0000% | 0 | +31,375 |

## Findings

1. **`100` causes the large probability/EV collapse.** It lowers best EV in all three hands, and lowers win/tenpai probabilities in all three. The first hand also changes the best discard.
2. **`001` never fires.** Vertices, edges, EV, win probability, and tenpai probability are exactly identical to `000` for all three hands. It nevertheless adds 90,060 / 91,720 / 31,375 `UnnecessaryTileCalculator` calls respectively.
3. **`010` has mixed correctness.** It preserves all reported values in the first hand; in the second it changes EV despite identical win/tenpai probabilities; in the third it lowers EV while probabilities remain identical. This points to policy/score-path changes rather than probability loss.
4. **Both graph construction and DP are material.** Across baselines, graph build is 40–66% and DP 31–50% of measured wall time; CSR is only about 2–3%. Optimizing only graph construction cannot deliver a 30x speedup.
5. The current noop implementation remains semantically unsafe by inspection because it tests only shanten-preserving discards, but that defect was not exercised by these three hands: its activation count is zero.

## Full per-discard data

Every discard/config combination, including EV, win probability, tenpai probability, timing, graph sizes, calculator calls, and rule counters, is in `deep_search_pruning_ablation_profile.csv`.

## Decision

- Reject `111` as an adoption candidate.
- Withdraw or redesign `100`; a 4+ shanten blanket cutoff is not acceptable.
- Keep `010` experimental until its EV-only changes are explained.
- Redesign `001` around the final allowed discard candidate set; do not spend more runtime on the current predicate.
- No pruning semantics were changed in this measurement commit.
