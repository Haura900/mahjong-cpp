# Optimization inventory

## Baseline and evidence

The drill-generator baseline is `engine-v0.9.13` / `e1ad6449c840f4d8181e7a7b8e0af3cbb57e4649`. This document distinguishes it from pre-packed development baselines: the often remembered **47 s → 10 s** result is real, but it compares a recorded pre-packed full calculation to the packed candidate's EV-only mode on `147m258p369s11122z`, `t_max=6`, `extra=1`, calls and turn-yaku enabled. It is a native Release measurement, not a WASM measurement.

## Adopted

| Optimization | Commit | Correctness evidence | Measured effect |
|---|---|---|---|
| Vectorized yaku DP / phase-memory release | `69e4d46` | existing regression suite | adopted release optimization |
| Single-precision graph state | `8d75a14` | release regression suite | lower graph-state memory |
| Non-recursive graph building | `a9dcf30` | release regression suite | avoids recursion stack pressure |
| Bounded improving chi/pon calls | `2201223`, `a22427d` | engine-v0.9.13 production semantics | bounds call expansion |
| Packed EdgeData + direct incoming CSR fill | `f3d22c7` | same 3,039,890 states / 11,077,833 edges, max EV difference 0 | full: 47.292 s → 13.241 s (3.57x); deterministic retained edge storage about -172 MiB |
| Exact exp-score-only DP | `90afc42` | same graph and EV/recommendation; only valid when caller needs EV | packed EV-only: 10.067 s (4.70x vs recorded old full) |
| `try_emplace` node cache insertion | `6c823fd` | no semantic change | retained as a small exact cache improvement |

The packed result, not a pruning rule, is the confirmed source of the large exact speedup. The browser artifact exposes the full-statistics API by default so A/B comparisons include win and tenpai probabilities.

## Rejected and removed

| Experiment | Reason |
|---|---|
| 4+ shanten blanket cutoff (`100`) | changes EV, win/tenpai, and a recommendation; see `deep_search_pruning_ablation_profile.md` |
| shanten-down multiple-discard pruning (`010`) | changes EV despite matching win/tenpai in cases |
| noop tegawari pruning (`001`) | no hits; adds calculator calls; unsafe predicate by inspection |
| exchange-distance caps D1–D3 | approximate or negligible state reduction; no useful frontier |
| deep-event count-state pruning | COUNT1 grows states +243.6%; COUNT2 exceeded 2.21 GiB |
| HandAnalysisCache | hash/retained-memory cost slowed deep search |

These are intentionally absent from the production request schema and `ExpectedScoreCalculator::Config`; Git history retains the experiments.

## Future candidates

* Exact turn-yaku single-graph representation: bounded theoretical gain because overlay graphs were only about 6% of graph-build time.
* Direct packed transition graph / graph-and-DP fusion: requires a separate correctness proof and full corpus benchmark before adoption.

Use `python scripts/compare_engine_versions.py --base engine-v0.9.13 --candidate HEAD --corpus benchmark_corpus/smoke.json --report compare.json` for an independent revision comparison. Exact candidates must retain 100% recommendation match and maximum output differences within `1e-5`.
