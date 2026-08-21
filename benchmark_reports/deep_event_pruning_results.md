# Deep-event pruning prototype results

This is default-off experimental code on `experiment/deep-event-pruning`; the exact baseline remains `feature/deep-search-pruning` at `90afc42`.

`147m258p369s11122z`, `t_max=6`, `extra=1`, calls/turn-yaku on:

| config | fast runtime | states | edges | state change | edge change | pruning hits | recommendation | fast/full EV diff |
|---|---:|---:|---:|---:|---:|---:|---|---:|
| BASE exact EV-only | 20.023 s | 3,039,890 | 11,077,833 | — | — | — | tile 0 | 0 |
| COUNT1 | 72.683 s | 10,445,443 | 31,739,127 | +243.6% | +186.5% | 1,597,203 | tile 0 | 0 |
| COUNT2 | aborted | >2.21 GiB in first pass | — | — | — | — | — | — |
| 4SH_COUNT1 | not run | same exact key expansion is required | — | — | — | — | — | — |

COUNT1 preserves EV-only/full equality for the same pruned graph but is not a speed optimization: accurate path-state separation overwhelms pruning. COUNT2 exceeded the safe memory threshold. 4SH_COUNT1 would retain the same expansion while pruning fewer paths, so it was stopped before consuming another unsafe run.

No `YakuDistanceEvaluator` was added. The scorer exposes completed score/yaku information, not cheap generic partial-hand decomposition distance. Exact candidate distance would add shape search to an already memory-bound prototype, while GraphBuilder-local yaku conditionals are explicitly avoided. `COUNT1_YAKU` is therefore not measured.

BASE is the only Pareto-safe config. This count-state design must not be enabled or refined without a materially smaller state summary and bounded-memory distribution collector.
