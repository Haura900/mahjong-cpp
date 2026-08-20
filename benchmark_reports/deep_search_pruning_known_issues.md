# Deep-search pruning experiment: known issues

## Status

The implementation on `feature/deep-search-pruning` is experimental and is not
ready to merge.  All three new flags default to `false`, so the baseline behavior
is preserved unless they are explicitly enabled.

The benchmark below compares the same build with only these flags changed:

- `000`: all three new pruning flags disabled
- `111`: all three new pruning flags enabled

Existing saved simulator results and historical problem answers were not used as
the baseline.

## Benchmark conditions

- Three generated four-shanten, closed-hand test cases
- Current turn: 6
- Engine default calculation end: turn 18
- Standard simulator hazards and `remaining_tiles = 48`
- `enable_shanten_down = true`
- `enable_tegawari = true`
- `auto_disable_deep_search = false`, so that the new pruning can be measured
- One run per configuration and hand
- Individual-run timeout: 60 seconds

## Results

| Hand | 000 time | 111 time | Speedup | 000 searched | 111 searched | Searched reduction | 000 best EV | 111 best EV | Best EV change | Recommendation |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `147m258p369s11122z` | 33.47 s | 22.23 s | 1.51x | 3,045,535 | 2,242,376 | 26.4% | 344.8274 | 163.8585 | -52.5% | `1m` -> `1m` |
| `133m188p469s12467z` | 13.78 s | 10.72 s | 1.29x | 1,443,133 | 1,141,140 | 20.9% | 117.3493 | 49.3655 | -57.9% | `4s/6s` -> `4s/6s` |
| `2359m23358p89s125z` | 35.73 s | 15.97 s | 2.24x | 3,817,538 | 1,769,682 | 53.6% | 127.2059 | 102.1132 | -19.7% | `5z` -> `2z` (near tie) |

Additional result changes:

| Hand | 000 win | 111 win | 000 tenpai | 111 tenpai |
|---|---:|---:|---:|---:|
| `147m258p369s11122z` | 6.58% | 3.08% | 21.16% | 11.92% |
| `133m188p469s12467z` | 0.62% | 0.49% | 3.92% | 3.35% |
| `2359m23358p89s125z` | 2.95% | 2.54% | 11.26% | 10.02% |

## Known problems

### Speed improvement is inconsistent and sometimes modest

The measured speedup ranges from 1.29x to 2.24x.  Two of the three optimized
runs still take more than ten seconds.  The change therefore does not yet solve
the high-shanten latency problem reliably.

### Calculation results change too much

The optimized best EV falls by 19.7% to 57.9%.  Win and tenpai probabilities
also decrease materially.  These changes are too large to treat as harmless
approximation noise.

The root discard candidate set differs between `000` and `111` for all three
hands.  This may violate the requirement that shanten-improving draws and normal
shanten-preserving discards remain available.

The likely failure mode is that pruning extensions at a high-shanten node also
removes valuable downstream paths after the hand later improves.  This is not
yet proven because the three flags have not been isolated with a full
`100`/`010`/`001` ablation on these cases.

## Required follow-up before merge

1. Run `100`, `010`, and `001` separately to identify which rule changes EV and
   the root candidate set.
2. Verify that every normal root discard in `000` is present in each pruned
   configuration.
3. Compare per-discard EV, win probability, and tenpai probability, not only
   runtime and `searched`.
4. Redesign or restrict the destructive rule before evaluating a larger corpus.
5. Do not enable any new pruning flag by default until the result differences
   are understood and accepted.
