# Exact fast-path follow-up (2026-08-21)

`origin/feature/deep-search-pruning` and local `HEAD` were both `90afc42b03f100da46aaf44575a56dbbd83603b7` before the experiment branch.

Local Release, `t_max=6`, `extra=1`, calls/turn-yaku on, three repetitions:

| hand | full ms | EV-only ms | fast graph ms | fast CSR ms | fast DP ms | states | edges | EV / recommendation |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `147m258p369s11122z` | 23289.783 | 20022.974 | 17725.872 | 659.726 | 1559.432 | 3,039,890 | 11,077,833 | exact / match |
| `133m188p469s12467z` | 12526.106 | 12053.962 | 11031.792 | 372.464 | 617.255 | 1,443,133 | 5,363,372 | exact / match |
| `2359m23358p89s125z` | 28572.881 | 25148.272 | 22430.289 | 711.979 | 1908.335 | 3,886,491 | 13,766,865 | exact / match |

The EV-only path is graph-build dominated (88.5%, 91.5%, and 89.2%). CSR is 3% or less, so a CSR-only change has no useful headroom. `EdgeData` is already 24 bytes with 32-bit IDs; further shrinking would alter score representation or separate fields needed by full statistics. The EV-only path also needs both scores for turn-yaku.

An instrumentation-only patch adds analyzer timings. A small regression hand remained exact: 4,772 states, 12,276 edges, maximum EV difference 0, matching recommendation. No exact change met the 5% adoption threshold, so the production baseline is unchanged.
