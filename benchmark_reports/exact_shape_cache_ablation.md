# Exact A: cross-pass shape cache

This is an exact optimization of the existing three-pass turn-yaku overlay.
The cache is private to one `ExpectedScoreCalculator::calc` call and is capped
at 65,536 entries per calculator kind so it cannot grow with the complete deep
search graph. It caches the existing `NecessaryTileCalculator` and
`UnnecessaryTileCalculator` results keyed by the already-used hand/meld cache
key. It changes neither graph construction nor DP reduction order.

## Native smoke result

Environment: MSVC 19.44.35228, CMake 3.31.6, Release, Windows x64.
Reference: `c30b181` (Adaptive removed, before Exact A).
Candidate: working tree with Exact A.

| case | reference | Exact A | speedup | states | edges | recommendation | max EV/win/tenpai delta |
|---|---:|---:|---:|---:|---:|---|---:|
| two-shanten-turn-yaku | 69.4 ms | 46.9 ms | 1.48x | 6,536 | 16,824 | match | 0 |
| three-shanten-turn-yaku-full | 1304.8 ms | 1188.0 ms | 1.10x | 368,665 | 1,159,375 | match | 0 |

The corresponding JSON, including all three core-invocation timings, is
[`exact_shape_cache_ablation.json`](exact_shape_cache_ablation.json). In that
run the calculator-call totals changed from 2,429 necessary / 4,109 unnecessary
to 757 / 1,394. In the full three-shanten case they changed from 157,621 /
211,046 to 62,053 / 113,631. Graph topology and returned statistics were
identical. The full-case raw result is
[`exact_three_shanten_full.json`](exact_three_shanten_full.json).

## Other exact candidates

Incremental table hashes, compact table storage, reserve tuning, and sharing
the turn-yaku topology require separate ablations. They are intentionally not
folded into Exact A: no unmeasured implementation is retained as a product
optimization.
