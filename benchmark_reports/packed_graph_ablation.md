# Packed graph ablation

## 2026-08-22: DrawEdge contribution hot/cold split

**Hypothesis.** `DrawEdge` was 32 bytes even when yaku/Shapley output was
disabled, because four optional contribution offset/count fields lived in the
DP hot representation. `Graph` already stored those values in an optional
sidecar, so copy them to an `EdgeCsr` sidecar only when contribution statistics
are requested.

**Change.** `DrawEdge` is now `{target, weight, score, last_score}` and has a
compile-time `sizeof(DrawEdge) == 16` assertion. `EdgeContributionData`
remains a 16-byte parallel array only for `calc_yaku_stats ||
calc_shapley_stats`. The selection CSR, insertion order, state graph, and
floating-point DP order are unchanged.

| Mode | DrawEdge bytes / edge | sidecar bytes / edge | CSR edge bytes / edge |
|---|---:|---:|---:|
| full yaku/Shapley | 16 | 16 | 32 |
| normal stats (no yaku/Shapley) | 16 | 0 | 16 |
| EV-only | 16 | 0 | 16 |

Thus standard full Shapley preserves its allocation and semantics, while the
normal and EV-only paths remove 16 bytes per draw edge from the compact CSR
(about 168 MiB for 11,077,833 edges). Native Release compilation passed with
MSVC 19.44.35228.

The direct v0.9.13 versus current-candidate smoke run is saved as
`packed_hotcold_smoke_20260822.json`: all three deep cases had identical
recommendations, states, EV, win, and tenpai values (maximum difference 0).
The end-to-end speedups were 0.86x, 1.21x, and 1.60x respectively. This is not
an isolated micro-ablation—the candidate includes the previously adopted packed
optimizations—so it is correctness evidence and not a claim that this one split
caused those speedups. The memory reduction is structural and applies only
when optional yaku/Shapley output is disabled.

No speculative CSR-direct build, source/next-out removal, score sparsification,
or SoA rewrite was retained in this change: each can alter duplicate detection,
edge order, or DP reduction order and needs a separate measured experiment.
