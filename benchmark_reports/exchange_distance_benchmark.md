# Exchange-distance pruning experiment

## Conditions

- Three documented 4-shanten hands, turn 6, Release + OpenMP build.
- `auto_disable_deep_search=false`; legacy `100` and `001` disabled.
- `BASE=-1`, `D1=1`, `D2=2`, `D3=3`.
- `D*+010` additionally enables `prune_shanten_down_with_multiple_discards`.
- One wall-time run per cell. Phase timers and graph sizes are more reliable than
  small wall-time differences.

## Main result

The exchange-distance cap is structurally safe to implement without a new cache-key
field, but the tested caps do not offer a useful accuracy/speed frontier on these
hands. D1 changes EV/probabilities while cutting only 3.0-4.5% of searched states.
D2 is exact on two hands and nearly exact on the third, but cuts at most 0.9%.
D3 is effectively the baseline. Combining the cap with 010 retains 010's EV error.

| hand | config | searched change | best EV change | max abs EV change | max win change | max tenpai change | recommendation |
|---|---|---:|---:|---:|---:|---:|---|
| `147m258p369s11122z` | D1 | -4.46% | -5.6069 | 6.1202 | 0.0600pp | 0.1600pp | unchanged (`9s`) |
|  | D2 | -0.91% | 0.0000 | 0.0000 | 0 | 0 | unchanged |
|  | D3 | -0.91% | 0.0000 | 0.0000 | 0 | 0 | unchanged |
| `133m188p469s12467z` | D1 | -3.32% | -0.0015 | 0.8021 | 0.0400pp | 0.1500pp | unchanged (`4s`/`6s`) |
|  | D2 | 0.00% | 0.0000 | 0.0000 | 0 | 0 | unchanged |
|  | D3 | 0.00% | 0.0000 | 0.0000 | 0 | 0 | unchanged |
| `2359m23358p89s125z` | D1 | -2.96% | -3.4973 | 4.5701 | 0.1500pp | 0.5100pp | unchanged (`2z`/`5z`) |
|  | D2 | -0.03% | -0.0308 | 0.0311 | 0 | 0 | unchanged |
|  | D3 | 0.00% | 0.0000 | 0.0000 | 0 | 0 | unchanged |

The D2/D3 equality in the first hand is not a typo: the pre-existing
`exchange_distance + shanten < shanten_org + extra` radius already prevents the
additional distance-3 expansion reached by this corpus.

## 010 quantitative result

| hand | searched | graph build | DP | wall | best EV | max abs EV change | recommendation |
|---|---:|---:|---:|---:|---:|---:|---|
| `147m258p369s11122z` | -1.35% | -6.91% | -13.52% | -10.53% | 0.0000 | 0.0213 (`5p`) | unchanged |
| `133m188p469s12467z` | -15.89% | -15.78% | -20.62% | -17.24% | +8.6040 | 8.6040 | unchanged |
| `2359m23358p89s125z` | -50.78% | -55.68% | -62.57% | -59.12% | -5.4172 | 5.4172 | unchanged |

Win and tenpai probabilities are identical for every discard in these three 010
runs. EV is not: the second hand increases and the third decreases. Therefore 010
is an approximation of score/policy paths, not a safe exact pruning.

## D + 010

- First hand: D2/D3+010 cut 5.32% searched; max EV error is 0.0526.
- Second hand: D2/D3+010 cut 24.91%; best EV remains +8.6040 versus baseline.
- Third hand: D1/D2/D3+010 cut 70.32%/64.80%/64.77%, but best EV errors are
  -9.9177/-6.3489/-6.3189. The cap does not repair 010's semantic error.

## Turn-yaku toggle

Turning `enable_turn_yaku` off preserves win and tenpai probabilities in all three
hands, but changes EV materially:

| hand | searched change | best EV change | max abs EV change | phase-time change |
|---|---:|---:|---:|---:|
| `147m258p369s11122z` | -5.02% | -5.9838 | 6.4673 | -6.0% |
| `133m188p469s12467z` | -22.60% | +1.7641 | 18.1960 | -32.3% |
| `2359m23358p89s125z` | -61.65% | -19.5238 | 21.9403 | -67.5% |

Phase time is graph + CSR + DP. The ON path performs the base calculation plus
turn-yaku overlays, so searched/time changes are expected. The EV deltas are too
large to disable turn yaku when matching the current EV definition is required.

## Exact exp-score-only fast path: code dependency review

An exact fast path is feasible and does not need a new graph or approximate value
function.

- Discard policy selection uses only `exp_score`.
- Call-option selection and acceptance use only the target and current `exp_score`.
- Draw recurrence, ippatsu expiry, ron expansion, other-win survival, and turn-yaku
  overlay each have an exp-score-only scalar recurrence.
- `tenpai_prob`, `win_prob`, `call_prob`, `call_win_prob`, and
  `call_tile_probability` are propagated for reporting; none feeds the exp-score
  choice or recurrence.
- Yaku/Shapley contribution arrays are already conditional on their corresponding
  flags and can remain outside the fast path.

Recommended design: add a separate `calc_exp_score_only` DP routine selected only
when the caller explicitly requests EV-only output. It should use a compact
`vector<float>` (or a dedicated one-field value object) indexed by vertex, retain
the small per-turn scratch values required for ron/turn-yaku, and write only
`Stat::exp_score`. Do not overload `calc_stats=false`: that flag currently skips the
calculation entirely in some API flows. Validate it by running both paths on the
same corpus and requiring bitwise or tight-tolerance EV equality and identical
recommendations.

## Throughput benchmark proposal

Use a fixed corpus and a fixed physical-core budget `N`. Run independent engine
processes with `OMP_NUM_THREADS` set per process:

1. `1 process x N threads`
2. `2 processes x N/2 threads`
3. `4 processes x N/4 threads`
4. `N processes x 1 thread`

Warm each process with one excluded request, then assign disjoint corpus shards.
Record total problems/second, p50/p95 latency, peak RSS, CPU utilization, and total
graph/CSR/DP time. Repeat each layout at least three times with shuffled corpus
order. Disable simultaneous multithreading for the first comparison or report
physical and logical core results separately. The benchmark driver should launch
one engine port per process and keep total OpenMP threads constant.

## Decision

- Keep `max_deep_exchange_distance` as an experimental, default-off knob.
- Do not adopt D1, D2, D3, or their 010 combinations as the production default
  based on this corpus.
- Keep 010 experimental; do not infer safety from identical win/tenpai values.
- Do not disable turn yaku if current EV fidelity is required.
- Prioritize implementing and benchmarking the exact exp-score-only DP path, then
  compare batch process layouts.

Full per-discard output is in `exchange_distance_benchmark.csv`; raw profiles and
all response rows are in `exchange_distance_benchmark.json`.
