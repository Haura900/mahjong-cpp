// Single source of truth for the benchmark UI.  Keep this in sync with
// data/config/request_schema.json; scripts/test_engine_benchmark_options.py
// enforces the calculation-option coverage.
export const DRILL_STANDARD_PRESET = Object.freeze({
  t_max: 18, extra: 1, enable_reddora: true, enable_uradora: true,
  enable_shanten_down: true, enable_tegawari: true,
  auto_disable_deep_search: true, enable_riichi: true, enable_calls: true,
  enable_turn_yaku: true, calc_stats: true, calc_exp_score_only: false,
  ron_rate: 0.7, enable_other_win_stop: true,
  other_win_hazard: [0.0002, 0.0008, 0.0029, 0.0078, 0.0170, 0.0305, 0.0467,
    0.0644, 0.0823, 0.0975, 0.1108, 0.1212, 0.1276, 0.1312, 0.1323,
    0.1309, 0.1170, 0.1170],
  calc_yaku_stats: true, calc_shapley_stats: true,
  // Omitted uses Config::yaku_filter's full NormalMask|YakumanMask|NukiDora.
  // JavaScript Number cannot faithfully represent that 57-bit mask.
  yaku_filter: '', state_tag: 0,
});

export const OPTION_METADATA = Object.freeze([
  {key:'enable_shanten_down', category:'探索範囲', label:'シャンテン戻し', type:'boolean', description:'シャンテンを戻す選択肢を探索します。'},
  {key:'enable_tegawari', category:'探索範囲', label:'手替わり', type:'boolean', description:'同一シャンテン内の手替わりを探索します。'},
  {key:'auto_disable_deep_search', category:'探索範囲', label:'深い手牌で探索を自動短縮', type:'boolean', description:'ONの場合、4シャンテン以上ではシャンテン戻し・手替わりを自動的に抑制します。'},
  {key:'extra', category:'探索範囲', label:'探索範囲', type:'number', min:0, max:2, description:'可能な交換のシャンテン数に加える探索余裕です。'},
  {key:'t_min', category:'探索範囲', label:'現在巡目', type:'number', min:1, max:18, scene:true, description:'計算結果を読む現在の巡目です。'},
  {key:'t_max', category:'探索範囲', label:'計算終端巡目', type:'number', min:1, max:18, description:'DPを計算する最終巡目です。'},
  {key:'enable_reddora', category:'麻雀ルール・得点', label:'赤ドラ', type:'boolean', description:'赤五牌をドラとして扱います。'},
  {key:'enable_uradora', category:'麻雀ルール・得点', label:'裏ドラ', type:'boolean', description:'立直和了時の裏ドラを得点に含めます。'},
  {key:'enable_riichi', category:'麻雀ルール・得点', label:'立直', type:'boolean', description:'門前テンパイ継続を立直として得点化します。'},
  {key:'enable_turn_yaku', category:'麻雀ルール・得点', label:'巡目依存役', type:'boolean', description:'一発・海底・河底を巡目依存の状態として計算します。'},
  {key:'enable_calls', category:'鳴き', label:'鳴き探索', type:'boolean', description:'シャンテン改善となるチー・ポンを将来の選択肢として探索します（最初の両面チー等は除外）。'},
  {key:'calc_stats', category:'出力統計', label:'基本統計', type:'boolean', description:'和了率・聴牌率・鳴き統計を計算します。', incompatibleWith:['calc_exp_score_only']},
  {key:'calc_yaku_stats', category:'出力統計', label:'役別統計', type:'boolean', description:'候補打牌ごとの役出現確率を計算します。', incompatibleWith:['calc_exp_score_only']},
  {key:'calc_shapley_stats', category:'出力統計', label:'役別Shapley', type:'boolean', description:'候補打牌ごとの役・ボーナス別Shapley期待値寄与を計算します。', incompatibleWith:['calc_exp_score_only']},
  {key:'calc_exp_score_only', category:'出力統計', label:'期待値のみ計算（高速）', type:'boolean', description:'期待値と推奨打牌のみを計算する高速モード。和了率・聴牌率・鳴き・役別統計・役別Shapleyは出力しません。', candidateOnly:true},
  {key:'ron_rate', category:'experimental / model adjustment', label:'ロン和了率', type:'number', min:0, max:1, step:.01, description:'完成和了のうちロンとして得点化する割合です。通常は変更不要です。'},
  {key:'remaining_tiles', category:'experimental / model adjustment', label:'残り山牌', type:'number', min:0, max:70, auto:true, description:'現在ツモ後のライブ山残り枚数。空欄なら現在巡目から自動計算します。'},
  {key:'enable_other_win_stop', category:'experimental / model adjustment', label:'他家和了で打切り', type:'boolean', description:'将来巡目で他家が和了した経路を停止します。通常は何切るドリル設定を使用します。'},
  {key:'other_win_hazard', category:'experimental / model adjustment', label:'他家和了ハザード', type:'json', description:'巡目1〜18の確率配列(JSON)。通常は変更不要です。'},
  {key:'yaku_filter', category:'experimental / model adjustment', label:'役フィルタ', type:'text', description:'役別統計に含める役・ボーナスのbit maskです。空欄はengine標準の全役です。通常は変更不要です。'},
  {key:'state_tag', category:'experimental / model adjustment', label:'状態タグ', type:'number', min:0, max:255, description:'将来の探索ポリシー用予約値です。通常は0です。'},
]);

export const SCENE_KEYS = Object.freeze(['game_mode','round_wind','seat_wind','dora_indicators','hand','melds','nuki_count','wall']);
