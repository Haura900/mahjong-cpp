# Regression-first expected-score extension

This branch extends `ExpectedScoreCalculator` while keeping upstream
`nekobean/mahjong-cpp` commit `453cae05caf0e3c0da13846f82c20685becaea6e`
as the numerical oracle.

## Compatibility contract

The defaults preserve the existing public `calc(...)` overloads and existing
`Stat` fields. In particular:

- `Config::ron_rate == 0.0` uses the legacy tsumo-only scoring path.
- `Config::calc_yaku_stats == false` does not allocate or run contribution DP.
- `Config::calc_shapley_stats == false` avoids exact terminal coalition
  enumeration and preserves the legacy fast path.
- existing melds remain represented by `PlayerState::melds`; the compact state
  key reserves `state_tag` for a future call-policy or dynamic-meld state index.
- JSON request extensions are optional, and optional response members are only
  emitted when their feature is enabled.

The regression fixture covers closed standard, seven-pairs, and open-meld
states. A larger fixture compares 35 returned choices over eight turns for
tenpai probability, win probability, and expected score. With `ron_rate=0`, the
maximum absolute difference from the upstream binary is `0.0`, and all searched
state counts and choice ordering match.

## Ron approximation

`ron_rate` is an approximate three-opponent discard model. For each DP turn,
let `p` be the original one-player tsumo probability and `s = 1 - ron_rate` be
the requested share of wins that are tsumo. If `x` is the ron probability on
each opponent discard, the model uses:

```text
ron   = x + (1-x)x + (1-x)^2 x = 1 - (1-x)^3
tsumo = (1-x)^3 p
(1-x)^3 = s / (s + (1-s)p)
```

Thus `ron_rate=0.7` models “30% of completed wins are tsumo”. Ron can occur on
the three opponent discards before the next draw; only when all three miss does
the original tsumo transition run. Win probability, expected score, yaku
occurrence and contribution series are all propagated with this survival
probability. Tenpai probability is unchanged. `ron_rate=0` removes the extra
discard hazards and exactly restores the upstream behavior.

## Yaku contribution API

Enable `Config::calc_yaku_stats` and inspect `Stat::yaku_stats`. Each entry has
turn-indexed series:

- `occurrence_prob`: probability that the expected-score-maximizing policy
  wins with the yaku. This is an event probability, not a score contribution.
- `inclusive_score`: expected score of wins that contain the yaku. Overlap is
  intentional, so these values are not additive.
- `marginal_score`: expected score lost when that yaku's value is removed from
  the terminal score calculation. This captures nonlinear mangan/haneman/etc.
  limits, but is not a Shapley allocation and is also not additive.
- `shapley_score`: exact Shapley allocation among every scoring yaku and bonus
  in the terminal result. Enable it with `Config::calc_shapley_stats`. With the
  complete default `yaku_filter`, the values sum to `Stat::exp_score` for every
  discard and turn, up to floating-point error.

All series are propagated through the same state graph and the same
score-maximizing policy as total expected score. Uradora uses the existing exact
indicator distribution: inclusive contribution conditions on one or more ura,
while marginal contribution compares against the zero-ura score.

Ippatsu, haitei and houtei are path-dependent. When tegawari is enabled, their
increment is evaluated by a separate turn-limited DP with tegawari state merging
disabled and overlaid on the normal tegawari result. This avoids counting the
same one-turn opportunity again when exchange paths merge, while retaining the
ordinary value of tegawari in the base result.

```cpp
ExpectedScoreCalculator::Config config;
config.ron_rate = 0.7;          // 30% tsumo, 70% ron among wins
config.remaining_tiles = 22;    // exact live-wall count at the current decision
config.calc_yaku_stats = true;
config.calc_shapley_stats = true;
config.yaku_filter = Yaku::Riichi | Yaku::Tanyao | Yaku::Pinfu |
                     Yaku::Dora | Yaku::RedDora |
                     Yaku::MixedTripleSequence | Yaku::PureStraight;

auto [stats, searched] = ExpectedScoreCalculator::calc(
    config, table_config, round_state, table_state, player);
```

When `remaining_tiles` is available, last-tile yaku are mutually exclusive.
If the live-wall count is divisible by four, the player's final opportunity is
a self draw and only haitei is enabled. Otherwise another player draws the last
tile and only houtei is enabled. If the live-wall count is unavailable, neither
last-tile yaku is guessed.

`enable_other_win_stop` applies a conditional other-player win hazard before
each future self-win opportunity. A stopped path contributes zero to self win
probability, expected score, and every yaku contribution. The 18 values are
provided by `other_win_hazard`; turn 18 deliberately reuses turn 17 so the
end-of-wall drop is not counted twice as both an opponent win and an exhaustive
draw. Disabling the option is regression-identical to the original DP.

`enable_calls` adds a deliberately bounded set of open-meld states to the same
memoized graph. The initial implementation considers only pon of dragons, the
round wind, or the seat wind, on each of the three opponent discard
opportunities. At every opportunity the DP compares calling (including the
mandatory following discard) with passing and keeps the higher expected-score
continuation. Meld contents are part of the cache identity. `Stat::call_prob`
reports the probability that the selected policy reaches a yakuhai-pon state.
With the option disabled no call states are built, preserving the legacy graph
and results.

`enable_probability_pruning` is independent of calls. When enabled, the engine
estimates the largest single-path probability of reaching every graph state
from the requested discard roots, covering draw, shanten-down, tegawari, and
call paths. States below `probability_prune_threshold` are omitted from all
value and yaku-contribution passes. Because many individually rare paths can
add up, this is an explicitly approximate mode: a higher threshold is faster
but less accurate. The default engine setting is disabled so legacy and
regression runs remain exact; callers can enable it explicitly for interactive
use.

For a terminal result with role set `N`, the coalition value `v(S)` is the
winner payment obtained when only the yaku/bonus categories in `S` contribute
han or yakuman multipliers. A coalition containing only dora-like bonuses has
value zero because it has no value yaku. Fu, nonlinear score limits, payment
rounding, honba, kyotaku, tsumo/ron, and the exact uradora distribution are all
included. With `v(empty)=0`, Shapley efficiency holds exactly. Roles are the
yaku in the full-score result; alternate hand decompositions are not added as
extra roles in that terminal cooperative game.

The implementation enumerates every terminal coalition (`2^K` for `K` scoring
categories), then propagates each exact allocation through the same
score-maximizing DP policy. It does not sample permutations or wins.

For JSON requests, the matching optional fields are `ron_rate`,
`calc_yaku_stats`, `calc_shapley_stats`, `yaku_filter`, `t_min`, `t_max`,
`extra`, `calc_stats`, and `state_tag`. Returned stat objects contain optional
`yaku_stats`.

## High-shanten calculation

When `auto_disable_deep_search=true` (the default) and the initial hand is four
shanten or deeper, the server forces `enable_shanten_down=false` and
`enable_tegawari=false` before graph construction. Set it to false to preserve
the caller's two expansion settings. The effective values are returned in the
response `config` object.

`enable_turn_yaku=true` enables turn-aware treatment of ippatsu, haitei, and
houtei. The graph distinguishes the first post-riichi draw from established
riichi states, and the DP uses dedicated last-turn score and contribution data.

The C++ engine accepts statistics searches above three shanten. For backward
compatibility and load safety, the server retains its old automatic cutoff when
`calc_stats` is omitted; send `"calc_stats": true` to opt in. High-shanten
searches can still be combinatorially large.
`extra=0` restricts the graph to shanten-progressing exchanges and is the
recommended starting point; `extra=1` retains one exchange of tegawari/shanten
down breadth. The regression test calculates a four-shanten hand with 15,833
states. A broader four-shanten probe with `extra=1` also completed with
1,791,822 states.

Windows builds reserve a 256 MiB stack for the recursive graph enumerator.

## Optimizations and benchmark

Implemented optimizations:

- two-word compact state key with incremental tile updates;
- bit iteration over only nonzero draw/discard candidates;
- incrementally maintained exchange distance;
- one-turn scalar DP state instead of three 19-turn arrays per vertex;
- contiguous graph/CSR storage retained for repeated turn and contribution
  passes; yaku edge data is sparse and only allocated when requested.

Release benchmark on Windows, Clang 22.1.8, 1,606,637-state fixture, three runs.
Values below are medians; memory is peak working set.

| Engine | Time | Peak memory |
|---|---:|---:|
| upstream baseline | 4,204.76 ms | 1,391,955,968 bytes |
| optimized | 3,029.01 ms | 830,459,904 bytes |

This is a 1.39x speedup (28.0% less time) and 40.3% lower peak memory. The
benchmark source is `sample_expected_score_regression.cpp`; the focused
high-shanten driver is `sample_expected_score_probe.cpp`.

## Tests

`test_expected_score_calculator` covers:

1. `ron_rate=0` upstream fixtures and choice ordering;
2. exact inclusive/marginal/Shapley values for a known pinfu/tanyao/sanshoku
   hand;
3. a four-shanten state;
4. ron/tsumo mixture invariants and invalid parameter handling;
5. Shapley efficiency for all 14 discards of `1234m23467p12406s`.

`test_json_parser` covers the optional request and response fields while
retaining the legacy response shape when extensions are disabled.
