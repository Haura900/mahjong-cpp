# Adaptive deep-search ablation

## Conclusion

**Do not replace Current Standard.** D preserves the full-search result at two
shanten in the checked full-stat/Shapley case, but it is far slower than the
published Standard preset at four shanten. It remains candidate-only and
experimental, not a production preset.

Raw results are in `adaptive_deep_search_{two,three,four}_shanten*.json`.
They use MSVC 19.44.35228, Release, t_min=6/t_max=8, one native server binary,
and `benchmark_corpus/adaptive_deep_search.json`.

| shanten | mode | wall time | states | edges | max EV/win/tenpai error vs F | recommendation |
|---:|---|---:|---:|---:|---:|---|
| 2 | S | 610 ms | 23,461 | 53,303 | 0 / 0 / 0 | match |
| 2 | F | 785 ms | 23,461 | 53,303 | reference | match |
| 2 | D | 690 ms | 23,461 | 53,303 | 0 / 0 / 0 | match |
| 3 | S | 11.108 s | 863,489 | 2,476,522 | 0 / 0 / 0 | match |
| 3 | F | 11.322 s | 863,489 | 2,476,522 | reference | match |
| 3 | D | 11.184 s | 847,733 | 2,390,195 | 0 / 0 / 0 | match |
| 4 | S | 236 ms | 8,884 | 27,194 | 0 / 0 / 0 | match |
| 4 | F | 29.509 s | 1,928,887 | 6,382,498 | reference | match |
| 4 | D | 24.697 s | 1,629,918 | 5,387,279 | 0 / 0 / 0 | match |

The two-shanten run enabled yaku occurrence and Shapley statistics; their
maximum difference from F was also zero. High-shanten raw runs omit those
expensive output passes, so this report makes no high-shanten Shapley claim.

## B/C/D ablation

At three shanten B/C/D were 11.413 / 10.911 / 11.184 s against F's 11.322 s.
D removed 18 shanten-down candidates and accepted 114 of 206 weighted-ukeire
tegawari candidates, but produced no useful speedup.

At four shanten B/C/D were 24.715 / 28.012 / 24.697 s against F's 29.509 s.
D is 1.19x faster than F, but roughly 105x slower than Standard. It accepted
1,159 of 2,363 tegawari candidates and re-entered full search 610,229 times.

The five-plus-shanten Full reference exceeded 1 GiB working set and was stopped
before host pressure. This is recorded as incomplete, not as a result.

## Semantics

`adaptive_deep_search_mode` is candidate-only: 0=off; 1=B (no shanten-down at
3+); 2=C (only weighted-ukeire-improving tegawari at 3+); 3=D (both). Every
nonzero mode returns to ordinary complete search at two shanten or less.
It deliberately overrides `auto_disable_deep_search`: the old initial-hand
shortcut cannot permanently disable the options before this re-entry occurs.
