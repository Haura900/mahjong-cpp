# Deep-event count analysis

A tegawari needs the shanten immediately before the draw and the identity of the drawn tile. The experimental discard cache key is therefore `hand + riichi + deep count + pre-draw shanten + drawn tile`; this prevents invalid merging of paths with different remaining budgets.

Shanten-back is recorded when a discard increases shanten relative to the pre-discard state. Tegawari is recorded when a non-improving draw is followed by discarding a different tile and final shanten returns to the pre-draw shanten. The counter resets only when final shanten improves.

The unbounded distribution run (`147m258p369s11122z`, sentinel budget 7) exceeded 2.17 GiB before completing and was stopped. Exact full-graph 2+ distribution data cannot honestly be reported from this state representation.

COUNT1 completed and gives this censored draw-state distribution (columns: deep event 0 / 1 / 2 / 3 / 4+):

| shanten | 0 | 1 | 2 | 3 | 4+ |
|---|---:|---:|---:|---:|---:|
| 0 | 138,000 | 1,344 | 0 | 0 | 0 |
| 1 | 544,681 | 126,198 | 0 | 0 | 0 |
| 2 | 347,956 | 138,902 | 0 | 0 | 0 |
| 3 | 44,661 | 31,586 | 0 | 0 | 0 |
| 4+ | 333 | 1,530 | 0 | 0 | 0 |
