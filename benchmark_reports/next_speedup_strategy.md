# ExpectedScoreCalculator: 次の高速化方針

> **Formal baseline (2026-08-21):** the compact transition graph and the exact
> `calc_exp_score_only` path are production features. Failed high-shanten,
> shanten-down, noop-tegawari and exchange-distance pruning experiments, the
> HandAnalysisCache, and unfinished oracle analysis are not part of the
> production implementation. Future comparisons use the approximately 10 s
> exact EV-only baseline, not the old 47 s implementation.

## 結論

次に実装すべきものは新たな枝刈りではない。`exp_score` だけを必要とする呼び出し用の **厳密な opt-in fast path** は実装済みである。turn-yaku 3-pass を個別計測した結果、巨大なのは base だけだった。次の第一候補はbase graph buildの direct packed transition graph化である。

この方針は近似を導入せず、探索する状態・遷移・`exp_score` の再帰式を変えない。`tenpai_prob`、`win_prob`、鳴き統計、役統計、Shapley を要求する既存 API は、従来経路のままにする。したがって通常の結果互換性リスクを opt-in 利用者に閉じ込められる。

5x を狙うには graph build と DP の両方を対象にする必要がある。10x は、現在の「全グラフを materialize してから全指標を DP する」設計を保った局所最適化だけでは現実的ではない。

## 実装状況

第一候補のうち、`Config::calc_exp_score_only=true` を実装した。同一の状態・遷移・score 選択を使いながら `exp_score` だけを返す。tenpai/win/call/役/Shapley 出力は空になり、役または Shapley 統計との併用はエラーにする。JSON API でも同名の boolean を指定できる。

手牌解析キャッシュも試作したが、深探索ではハッシュ lookup と保持メモリのコストが解析の再利用益を上回ったため撤回した。現在の実装には含めていない。

full path と fast path の `exp_score`、探索状態数を比較する回帰テストを追加した。対象は `06m27p2357s134457z`、`026m67p23578s4457z`、`347789m589p3s5677z` であり、calls と turn-yaku を有効にしている。

### 実測（2026-08-20）

Release、Yonma、東場・南家・東ドラ表示、`t_max=4`、`extra=0`、calls/turn-yaku 有効、各 mode を3回実行した。この小規模測定は、後に撤回した手牌解析キャッシュを含む試作時の値であるため、現行実装の採用判断には下の深探索測定を使う。

| hand | full ms | fast ms | total speedup | full DP ms | fast DP ms | DP speedup | states / edges | 最大 EV 差 | 推奨一致 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `06m27p2357s134457z` | 121.649 | 115.391 | 1.054x | 9.398 | 4.423 | 2.124x | 39,792 / 113,976 | 0 | yes |
| `026m67p23578s4457z` | 16.562 | 16.607 | 0.997x | 0.666 | 0.399 | 1.669x | 4,772 / 12,276 | 0 | yes |
| `347789m589p3s5677z` | 32.391 | 31.282 | 1.035x | 3.033 | 1.333 | 2.276x | 12,146 / 28,248 | 0 | yes |
| 合計（各1回換算） | 170.602 | 163.280 | 1.045x | 13.097 | 6.155 | 2.128x | — | 0 | yes |

fast path は DP 内で不要な確率・call distribution の計算をなくすため DP を約2.13x短縮した。しかし、この小さめの回帰手牌では graph build が wall time の約89–94%であり、全体では約1.05xに留まる。一方で精度は3手牌・全打牌・全 turn で完全一致した。

### 4シャンテン深探索の実測（現行実装）

`147m258p369s11122z`、`t_max=6`、`extra=1`、`auto_disable_deep_search=false`、calls/turn-yaku 有効、各 mode 1回で測定した。

| 指標 | full | exp-score-only | 比較 |
|---|---:|---:|---:|
| wall time | 54,026.139 ms | 47,671.233 ms | **1.133x** |
| graph build | 36,841.292 ms | 41,927.978 ms | 0.879x |
| DP | 14,478.002 ms | 3,460.600 ms | **4.184x** |
| states | 3,039,890 | 3,039,890 | 一致 |
| edges | 11,077,833 | 11,077,833 | 一致 |
| 最大 `exp_score` 差 | — | 0 | 完全一致 |
| 推奨打牌 | tile 0 | tile 0 | 一致 |

graph build 時間には測定ぶれがあるが、DP 短縮は大きく、全体でも約13%短縮した。反対に、試作した手牌解析キャッシュでは同じ手の fast graph build が 43,134 ms に悪化し、全体も 0.616x になったため採用していない。

### turn-yaku 3-pass の内訳（2026-08-21）

同じ4シャンテン手を再測定した。`base` はtegawariを含む通常探索、`turn_on` / `turn_off` はippatsu等のoverlay用でtegawariを止めた参照計算である。

| mode / invocation | graph ms | CSR ms | DP ms | draw vertices | discard vertices | edges |
|---|---:|---:|---:|---:|---:|---:|
| full / base | 29,067.015 | 2,140.392 | 13,471.742 | 999,668 | 1,887,355 | 10,576,585 |
| full / turn-on | 1,297.409 | 13.606 | 23.444 | 44,650 | 33,015 | 254,642 |
| full / turn-off | 1,052.875 | 27.789 | 42.086 | 37,160 | 38,041 | 246,606 |
| fast / base | 32,453.126 | 1,274.843 | 1,742.316 | 999,668 | 1,887,355 | 10,576,585 |
| fast / turn-on | 977.267 | 27.713 | 24.947 | 44,650 | 33,015 | 254,642 |
| fast / turn-off | 955.238 | 12.165 | 10.109 | 37,160 | 38,041 | 246,606 |
| merge overlay | 0.003 ms full / 0.001 ms fast | — | — | — | — | — |

full は 47.292秒、fast は 37.553秒、EV差 0、推奨打牌・states・edges は一致した。run間のgraph時間には温度・allocator等による揺れがあるため、同一run内の構成比で判断する。

**結論:** 3-pass は存在するが、巨大なのは base だけである。fastでは turn-on/off の graph build合計は 1.933秒（総graph buildの約5.6%）であり、単一graph化の全体上限はおおむね 1.06x 程度にとどまる。exact性を保つための設計候補としては残すが、次の第一実装候補ではない。

従って次の exact 構造変更の優先順位は以下とする。

1. **direct packed transition graph** — baseの linked adjacency、`Graph::add_edge`、edge metadata、hash/allocator局所性をまとめて減らせるため最有望。
2. **graph build と最終compact representationのfusion** — 中間 `Graph` からCSRへの二重表現・解放をなくし、1の実装経路として進める。
3. **explicit edge materialization削減** — 最も大きな改善余地はあるが、target ID再計算やDPとの融合を伴うため、1/2の計測後に判断する。

再現用の実行ファイルは `sample_expected_score_fastpath_benchmark HAND [T_MAX] [REPEAT] [DEEP_SEARCH]` である。最後を `1` にすると `extra=1` と `auto_disable_deep_search=false` で深探索を測る。

## 根拠にした計測

既存の Release 計測 `deep_search_pruning_ablation_profile.md` の `000`（枝刈りなし）3局を使った。設定は turn 6、`auto_disable_deep_search=false`、`enable_turn_yaku=true` であり、後者のためプロフィール値は内部の overlay を含む。

| hand | wall ms | graph build ms | DP ms | CSR ms | graph share | DP share |
|---|---:|---:|---:|---:|---:|---:|
| `147m258p369s11122z` | 25,563.0 | 11,986.7 | 12,706.0 | 829.7 | 46.9% | 49.7% |
| `133m188p469s12467z` | 9,780.8 | 6,448.2 | 3,009.1 | 307.6 | 65.9% | 30.8% |
| `2359m23358p89s125z` | 31,212.9 | 15,214.5 | 15,046.6 | 900.0 | 48.7% | 48.2% |
| weighted total | 66,556.7 | 33,649.4 | 30,761.7 | 2,037.3 | 50.6% | 46.2% |

CSR は約 3.1% に過ぎない。CSR だけをゼロコストにしても Amdahl 上限は約 1.03x であり、優先対象ではない。graph build だけを無限に速くしても上限は約 2.0x、DP だけでも約 1.86x である。

同じ資料では基準ケースが 1.44M–3.89M vertices、5.36M–13.77M edges に達している。従って、`reserve`、ハッシュ関数、CSR のような局所改善は必要でも、単独で桁違いの改善にはならない。

上表はコミット `5255b85` で追加された、現ブランチに保存済みのベースライン計測である。今回追加した実測は本レポート冒頭の「実測」に記録した。両者は手牌・巡目が異なるため、同じ表に混ぜず、それぞれの条件内で比較する。

## 現行パスの観察

1. `GraphBuilder::build_node` は到達した各 draw/discard state ごとに `NecessaryTileCalculator` または `UnnecessaryTileCalculator` を呼び、`Graph` と二つの node cache を構築する。
2. 構築後に `build_edge_csr` が adjacency をコピーし、`calc_stats` が全 turn・全 draw/discard vertex を走査する。通常 DP は `tenpai_prob`、`win_prob`、`exp_score`、call probability、call-win probability、call-tile distribution を常に更新する。
3. `enable_turn_yaku && enable_tegawari && calc_stats` では、現実装は base / turn-on / turn-off の **3 回** `calc_core` を実行して overlay を合成する。
4. `calc_yaku_stats=false` かつ `calc_shapley_stats=false` のときは edge contribution を作らないが、上記の通常 DP 指標は引き続き計算する。

特に 2 と 3 は、方針用に `exp_score` しか使わないリクエストが現在も全出力用のコストを払うことを意味する。

## 優先順位

| rank | 方針 | exactness | 主対象 | 現実的な単独効果 | 主要リスク |
|---:|---|---|---|---:|---|
| 1 | `exp_score`-only fast path | exact | DP | DP 2–4x、全体は graph build 次第 | 新旧 path の浮動小数差、API の意味 |
| 2 | turn-yaku overlay の単一グラフ化 | exact | graph build + DP | 1.3–2.5x（overlay 条件のみ） | ippatsu/haitei の状態同値性 |
| 3 | packed transition graph / fused DP | exact | メモリ帯域、CSR、allocation | 1.2–1.8x | 実装範囲が大きい |

数値は実装前の見積りであり、性能保証ではない。各候補は下記の段階計測を通過してから採用する。

### 1. `exp_score`-only fast path

新しい opt-in config（例: `OutputMode::ExpScoreOnly`）を追加する。指定時だけ、次を省く。

- `tenpai_prob`、`win_prob`、call 系の state/出力配列
- call-tile probability の頂点数 × call-tile 数の配列
- 役統計と Shapley（指定との併用は validation error にする）
- それらだけのための edge contribution

残すものは `exp_score` の値、選択規則、score 計算、同一の状態・遷移、および `enable_turn_yaku` 時の既存 overlay 意味論である。`calc_stats` を「一つの scalar expected-score DP」と「完全統計 DP」に分離すると、内側 edge loop のロード・演算・書き込みを大きく減らせる。turn-yaku overlay の単一化は第2候補であり、fast path 単独では既存の 3-pass 意味論を保持する。

独立した `HandAnalysisCache` も試したが、数百万状態の深探索では lookup/メモリ保持が支配的になり、graph build を悪化させた。従ってこのキャッシュは採用しない。

完全統計 API は一切置換しない。

**必須の検証:** 固定 corpus とランダム局面で、fast path の各 discard・各 turn の `exp_score` を既存 path と比較する。許容差は float storage に由来する既存の丸めを踏まえて決め、top-1 と同率集合も一致させる。profile には fast/full 別の graph/CSR/DP 時間、vertices、edges、analysis cache hit/miss を出す。

### 2. turn-yaku overlay の単一グラフ化

現在の 3 回の `calc_core` と `merge_turn_yaku_overlay` を、ippatsu の残存状態を明示した単一 state space と単一 backward DP に置き換える。tegawari を許した state と ippatsu だけを失効させた state を正しく区別して memoize し、overlay は作らない。

これは exact だが、同じ CacheKey にまとめてよい状態の判定が核心である。現在 triple-run になっている理由自体が「無条件に state を統合すると ippatsu が膨らむ」ことなので、安易な cache key 削減は不可。まず小さな参照実装として旧 overlay と全統計を比較し、`enable_turn_yaku` のみの opt-in で出荷する。

### 3. packed transition graph と fused DP

グラフ構築時の linked adjacency (`next_out` / `next_in`) を中間表現にせず、頂点ごとの連続 edge range を直接確保する。DP に不要な metadata を output mode ごとに分離し、graph build 後の CSR コピーと `release_adjacency` をなくす。さらに transition analysis を不変テーブルとして分離すれば、node cache、edge、DP state のメモリ局所性が良くなる。

これは正確性を保てるが、到達状態そのものを減らさない。プロファイル上 CSR は小さいので、狙いは CSR 時間ではなく、数百万 edge に対する build/DP の cache miss と allocation を減らすことにある。1番の fast path に必要な edge 最小化の後で行う。

## 5x / 10x の見通し

weighted phase share を graph 50.6%、DP 46.2%、CSR/その他 3.1% と置く。

| 仮定 | 全体 speedup |
|---|---:|
| CSR を完全に除去 | 1.03x |
| graph build だけを 5x | 1.68x |
| DP だけを 5x | 1.59x |
| graph build と DP をともに 5x | 4.52x |
| graph build と DP をともに 10x | 7.89x |

よって 5x は「exp-score-only で DP の仕事を減らす」ことと「解析再利用で graph build を減らす」ことを組み合わせて初めて射程に入る。10x はこの二つが目標を超えて効いた場合でも不足し得るため、単一グラフ化またはデータ表現の刷新まで必要になる。近似 boundary value、beam、Monte Carlo はこの目標を満たす手段ではないので、この段階では採用しない。

## 次の実装順

1. 非破壊の phase + analysis-cache-hit/miss 計測を追加し、代表3局・通常局面群・turn-yaku on/off を各3回以上測る。最初に calculator 呼び出し時間と DP scalar 化の余地を数値化する。
2. `exp_score`-only fast path を feature flag として実装し、full path との bit/許容差比較テストと recommendation 一致テストを追加する。
3. fast path 単独で 5x に届かないことを確認してから、turn-yaku 単一グラフ化の設計・参照比較に進む。

## 今回見送るもの

- 新しい枝刈り、high-shanten cutoff、exchange-distance cutoff
- boundary value、学習器、beam、Monte Carlo 等の近似
- 並列化、GPU、multi-process
- persistent cache（単発計算の高速化には直接効かない）
- 先行した CSR 専用最適化

これらは今回の「単一・厳密計算を速くする」目的か、計測で示された支配的コストに合わない。

## Packed build implementation (2026-08-21)

### Measured bottleneck and memory model

The old build representation stored both outgoing and incoming linked lists while
also collecting yaku contribution metadata for every edge, even when neither
`calc_yaku_stats` nor `calc_shapley_stats` was requested. On the representative
base graph this means 10,576,585 edges. `EdgeData` was 40 bytes (about 403 MiB),
the two linked-list heads cost another about 23 MiB, and the later CSR contains
about 323 MiB of `DrawEdge` plus 40 MiB of `SelectionEdge`.

The profile shows that the base invocation is the only large graph: turn-on and
turn-off together are only about 6% of graph-build time. Consequently, a
turn-yaku single graph has a roughly 1.06x ceiling and was not implemented.

### Adopted design: packed-build variant D

* `EdgeData` is now 24 bytes: source, target, outgoing link, weight, score and
  last-turn score. The incoming link is removed.
* Incoming selection CSR counts are accumulated directly from the edge array;
  filling that array in reverse insertion order preserves the old incoming-list
  order and floating-point reduction order.
* Contribution offsets/counts are held in a separate 16-byte array only when
  yaku or Shapley statistics were requested. Normal EV and exp-score-only
  calculations no longer scan or write per-yaku data at every edge.
* The existing CSR is retained, so this prototype does not eliminate final-DP
  storage. It reduces graph-build allocation and reverse-adjacency footprint.

This reduces normal build-time edge storage by 16 bytes/edge (about 161 MiB for
the representative base graph) and removes its 11 MiB incoming-head array.

### Release benchmark

Command: `sample_expected_score_fastpath_benchmark 147m258p369s11122z 6 3 1`.
The new values are the mean of three same-build runs. The old measurement was a
saved pre-change run with the same hand/settings.

| metric | old | new full | new exp-score-only |
|---|---:|---:|---:|
| total wall time | 47,291.804 ms | 13,240.942 ms | 10,066.949 ms |
| base graph build | 29,067.015 ms | 7,874.351 ms | 8,057.359 ms |
| base CSR/flatten | 2,140.392 ms | 418.603 ms | 434.116 ms |
| DP | 13,471.742 ms | 4,338.813 ms | 951.518 ms |
| states | 3,039,890 | 3,039,890 | 3,039,890 |
| edges | 11,077,833 | 11,077,833 | 11,077,833 |
| maximum absolute EV difference | n/a | 0 | 0 |

Base graph build is **3.69x faster** (full path), total full calculation is
**3.57x faster**, and the packed plus exp-score-only path is **4.70x faster**
than the recorded pre-change full baseline. The old run was not RSS-instrumented,
so a comparable old/new peak-RSS number cannot be reported honestly. The
deterministic retained-storage reduction is about 172 MiB before CSR/DP temporary
arrays.

### Exactness and regression

`test_expected_score_calculator` passed all 28 test cases (616 assertions),
including full yaku/Shapley paths, the existing light fast-path corpus, and the
three slow four-shanten cases:

* `147m258p369s11122z`
* `133m188p469s12467z`
* `2359m23358p89s125z`

For the representative benchmark, state count, transition count, every reported
discard EV, and the recommendation matched exactly. Reverse CSR fill preserves
the previous linked-list ordering.

### Updated next bottleneck

The Phase-1 success threshold is exceeded decisively, so the next exact target
is no longer allocator tuning. Full stats now spend roughly one third of the
representative runtime in DP; exp-score-only spends roughly 80% in graph build.
The next exact experiment should be graph-build/DP fusion or retaining only the
minimum target/weight representation needed by the scalar DP. If that cannot
reduce remaining graph-build time by at least 1.2x, stop layout work and move to
boundary-value approximation rather than pruning subtrees as self-loops.
