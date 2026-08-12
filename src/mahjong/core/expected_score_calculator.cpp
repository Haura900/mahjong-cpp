#include "expected_score_calculator.hpp"

#include <algorithm> // max, fill
#include <cassert>
#include <cmath>
#include <stdexcept>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "mahjong/core/necessary_tile_calculator.hpp"
#include "mahjong/core/score_calculator.hpp"
#include "mahjong/core/shanten_calculator.hpp"
#include "mahjong/core/unnecessary_tile_calculator.hpp"
#include "mahjong/core/utils.hpp"

namespace mahjong
{

namespace
{
std::uint64_t meld_signature(const std::vector<Meld> &melds)
{
    std::vector<std::uint64_t> encoded;
    encoded.reserve(melds.size());
    for (const auto &meld : melds) {
        std::vector<int> tiles = meld.tiles;
        std::sort(tiles.begin(), tiles.end());
        std::uint64_t value = static_cast<std::uint64_t>(meld.type & 7);
        for (const int tile : tiles) {
            value = value * 41 + static_cast<std::uint64_t>(tile + 1);
        }
        encoded.push_back(value);
    }
    std::sort(encoded.begin(), encoded.end());
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto value : encoded) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}
} // namespace

ExpectedScoreCalculator::CacheKey::CacheKey(const MergedCount &hand,
                                            const std::uint8_t riichi_state,
                                            const std::uint8_t state_tag)
{
    for (int tile = 0; tile < 18; ++tile) {
        lo |= static_cast<std::uint64_t>(hand[tile]) << (tile * 3);
    }
    for (int tile = 18; tile < 34; ++tile) {
        hi |= static_cast<std::uint64_t>(hand[tile]) << ((tile - 18) * 3);
    }
    for (int tile = 34; tile < 37; ++tile) {
        hi |= static_cast<std::uint64_t>(hand[tile]) << (48 + tile - 34);
    }
    hi |= static_cast<std::uint64_t>(riichi_state) << 51;
    hi |= static_cast<std::uint64_t>(state_tag) << 53;
}

ExpectedScoreCalculator::CacheKey ExpectedScoreCalculator::CacheKey::with_riichi_state(
    const std::uint8_t riichi_state) const noexcept
{
    CacheKey key = *this;
    key.hi = (key.hi & ~(std::uint64_t{3} << 51)) |
             (static_cast<std::uint64_t>(riichi_state) << 51);
    return key;
}

void ExpectedScoreCalculator::CacheKey::change_tile(const int tile,
                                                    const int delta) noexcept
{
    if (tile < 18) {
        const std::uint64_t unit = std::uint64_t{1} << (tile * 3);
        if (delta > 0) {
            lo += unit;
        }
        else {
            lo -= unit;
        }
    }
    else if (tile < 34) {
        const std::uint64_t unit = std::uint64_t{1} << ((tile - 18) * 3);
        if (delta > 0) {
            hi += unit;
        }
        else {
            hi -= unit;
        }
    }
    else {
        const std::uint64_t unit = std::uint64_t{1} << (48 + tile - 34);
        if (delta > 0) {
            hi += unit;
        }
        else {
            hi -= unit;
        }
    }
}

namespace
{

inline constexpr std::uint8_t NoRiichi = 0;
inline constexpr std::uint8_t IppatsuEligible = 1;
inline constexpr std::uint8_t RiichiEstablished = 2;
inline constexpr int LastTurn = 18;
inline constexpr std::uint32_t NoVertex = std::numeric_limits<std::uint32_t>::max();

int first_tile(const std::uint64_t mask) noexcept
{
#ifdef _MSC_VER
    unsigned long index = 0;
    _BitScanForward64(&index, mask);
    return static_cast<int>(index);
#else
    return __builtin_ctzll(mask);
#endif
}

int popcount(const std::uint64_t mask) noexcept
{
#ifdef _MSC_VER
    return static_cast<int>(__popcnt64(mask));
#else
    return __builtin_popcountll(mask);
#endif
}

std::uint64_t nonzero_mask(const SeparatedCount &counts) noexcept
{
    std::uint64_t mask = 0;
    for (int tile = 0; tile < 37; ++tile) {
        if (counts[tile] > 0) {
            mask |= std::uint64_t{1} << tile;
        }
    }
    return mask;
}

MergedCount to_merged_count(const SeparatedCount &counts)
{
    MergedCount merged = counts;
    merged[Tile::Manzu5] += merged[Tile::RedManzu5];
    merged[Tile::Pinzu5] += merged[Tile::RedPinzu5];
    merged[Tile::Souzu5] += merged[Tile::RedSouzu5];
    return merged;
}

SeparatedCount to_separated_count(const MergedCount &counts)
{
    SeparatedCount ret{0};
    for (int i = 0; i < 34; ++i) {
        ret[i] = counts[i];
    }

    if (counts[Tile::RedManzu5]) {
        --ret[Tile::Manzu5];
        ++ret[Tile::RedManzu5];
    }
    if (counts[Tile::RedPinzu5]) {
        --ret[Tile::Pinzu5];
        ++ret[Tile::RedPinzu5];
    }
    if (counts[Tile::RedSouzu5]) {
        --ret[Tile::Souzu5];
        ++ret[Tile::RedSouzu5];
    }

    return ret;
}

void normalize_red_fives(PlayerState &player)
{
    player.hand[Tile::RedManzu5] = 0;
    player.hand[Tile::RedPinzu5] = 0;
    player.hand[Tile::RedSouzu5] = 0;

    for (auto &meld : player.melds) {
        for (auto &tile : meld.tiles) {
            tile = Tile::to_normal(tile);
        }
    }
}

void normalize_red_fives(TableConfig &table_config, TableState &table_state)
{
    for (auto &tile : table_state.dora_indicators) {
        tile = Tile::to_normal(tile);
    }
    for (auto &tile : table_state.uradora_indicators) {
        tile = Tile::to_normal(tile);
    }
    table_config.rule_flags &= ~RuleFlag::RedDora;
}

void normalize_red_fives(MergedCount &wall)
{
    wall[Tile::RedManzu5] = 0;
    wall[Tile::RedPinzu5] = 0;
    wall[Tile::RedSouzu5] = 0;
}

void draw(PlayerState &player, SeparatedCount &hand_counts, SeparatedCount &wall_counts,
          const int tile)
{
    ++hand_counts[tile];
    --wall_counts[tile];
    player.hand[tile]++;
    if (tile == Tile::RedManzu5) {
        player.hand[Tile::Manzu5]++;
    }
    else if (tile == Tile::RedPinzu5) {
        player.hand[Tile::Pinzu5]++;
    }
    else if (tile == Tile::RedSouzu5) {
        player.hand[Tile::Souzu5]++;
    }
}

void discard(PlayerState &player, SeparatedCount &hand_counts,
             SeparatedCount &wall_counts, const int tile)
{
    --hand_counts[tile];
    ++wall_counts[tile];
    player.hand[tile]--;
    if (tile == Tile::RedManzu5) {
        player.hand[Tile::Manzu5]--;
    }
    else if (tile == Tile::RedPinzu5) {
        player.hand[Tile::Pinzu5]--;
    }
    else if (tile == Tile::RedSouzu5) {
        player.hand[Tile::Souzu5]--;
    }
}

double combination(const int n, const int r)
{
    if (r < 0 || r > n) {
        return 0.0;
    }

    const int k = std::min(r, n - r);
    double value = 1.0;
    for (int i = 1; i <= k; ++i) {
        value *= static_cast<double>(n - k + i);
        value /= static_cast<double>(i);
    }
    return value;
}

std::array<double, 13> calc_uradora_distribution(const MergedCount &wall,
                                                 const MergedCount &hand_and_melds,
                                                 const int num_indicators,
                                                 const int game_mode)
{
    std::array<std::array<double, 13>, 6> dp{};
    dp[0][0] = 1.0;

    for (int tile = 0; tile < 34; ++tile) {
        const int indicator = Tile::to_indicator(tile, game_mode);
        if (indicator == Tile::Null) {
            continue;
        }

        const int count = wall[indicator];
        const int gain = hand_and_melds[tile];
        if (count == 0) {
            continue;
        }

        auto next = dp;
        for (int selected = 0; selected < num_indicators; ++selected) {
            for (int uradora = 0; uradora <= 12; ++uradora) {
                if (dp[selected][uradora] == 0.0) {
                    continue;
                }
                const int max_take = std::min(count, num_indicators - selected);
                for (int take = 1; take <= max_take; ++take) {
                    const int next_selected = selected + take;
                    const int next_uradora = std::min(12, uradora + gain * take);
                    next[next_selected][next_uradora] +=
                        dp[selected][uradora] * combination(count, take);
                }
            }
        }
        dp = next;
    }

    const double denominator = combination(
        static_cast<int>(std::accumulate(wall.begin(), wall.begin() + 34, 0)),
        num_indicators);
    assert(denominator > 0.0);

    std::array<double, 13> probabilities{};
    for (int uradora = 0; uradora <= 12; ++uradora) {
        probabilities[uradora] = dp[num_indicators][uradora] / denominator;
    }
    return probabilities;
}

double calc_uradora_score(const ExpectedScoreCalculator::Config &config,
                          const TableConfig &table_config,
                          const RoundState &round_state, const TableState &table_state,
                          const PlayerState &player, const SeparatedCount &hand_counts,
                          const SeparatedCount &wall_counts, const ScoreResult &result,
                          const int win_flag)
{
    const int num_indicators = table_state.dora_indicators.size();

    // 裏ドラ表示牌の抽選では、赤5と通常5を同じ牌として扱う。
    const MergedCount wall = to_merged_count(wall_counts);
    MergedCount hand_and_melds = to_merged_count(hand_counts);
    for (const auto &meld : player.melds) {
        for (auto tile : meld.tiles) {
            ++hand_and_melds[Tile::to_normal(tile)];
        }
    }

    // 裏ドラ枚数ごとの確率と、各枚数での点数を掛け合わせる。
    const auto uradora_probabilities = calc_uradora_distribution(
        wall, hand_and_melds, num_indicators, table_config.game_mode);
    const std::vector<int> up_scores = ScoreCalculator::get_up_scores(
        table_config, round_state, table_state, player, result, win_flag, 12);

    double score = 0;
    for (int i = 0; i <= 12; ++i) {
        score += up_scores[i] * uradora_probabilities[i];
    }

    return score;
}

double calc_score(const ExpectedScoreCalculator::Config &config,
                  const TableConfig &table_config, const RoundState &round_state,
                  const TableState &table_state, PlayerState &player,
                  SeparatedCount &hand_counts, SeparatedCount &wall_counts,
                  const int shanten_type, const int win_tile, const bool riichi)
{
    // 期待値計算では和了を自摸和了として評価する。
    int win_flag = riichi ? (WinFlag::Tsumo | WinFlag::Riichi) : WinFlag::Tsumo;

    ScoreResult result =
        ScoreCalculator::calc_fast(table_config, round_state, table_state, player,
                                   win_tile, win_flag, shanten_type);

    // 役なしの場合は0点とする。
    if (!result.success) {
        return 0.0;
    }

    // 裏ドラ期待値を計算しない場合は、通常の和了点を返す。
    if (!config.enable_uradora || !(win_flag & WinFlag::Riichi) ||
        table_state.dora_indicators.empty()) {
        return result.payments[0];
    }

    // 役満以上は裏ドラで点数が変わらない。
    if (result.score_limit >= ScoreLimit::CountedYakuman) {
        return result.payments[0];
    }

    return calc_uradora_score(config, table_config, round_state, table_state, player,
                              hand_counts, wall_counts, result, win_flag);
}

template <std::size_t N>
std::vector<double> to_vector(const std::array<double, N> &values, const int t_max)
{
    assert(t_max >= 0 && t_max < static_cast<int>(N));
    return {values.begin(), values.begin() + t_max + 1};
}

bool has_value_yaku(const ScoreResult &result, const YakuFlags removed = Yaku::None)
{
    constexpr YakuFlags bonus =
        Yaku::Dora | Yaku::UraDora | Yaku::RedDora | Yaku::NukiDora;
    for (const auto &entry : result.yaku_list) {
        if (entry.yaku != removed && !(entry.yaku & bonus) && entry.han > 0) {
            return true;
        }
    }
    return false;
}

double final_score(const ExpectedScoreCalculator::Config &config,
                   const TableConfig &table_config, const RoundState &round_state,
                   const TableState &table_state, const PlayerState &player,
                   const SeparatedCount &hand_counts, const SeparatedCount &wall_counts,
                   const ScoreResult &result, const int win_flag)
{
    if (!result.success || !has_value_yaku(result)) {
        return 0.0;
    }
    if (!config.enable_uradora || !(win_flag & WinFlag::Riichi) ||
        table_state.dora_indicators.empty() ||
        result.score_limit >= ScoreLimit::CountedYakuman) {
        return result.payments[0];
    }
    return calc_uradora_score(config, table_config, round_state, table_state, player,
                              hand_counts, wall_counts, result, win_flag);
}

double score_for_han(const TableConfig &table_config, const RoundState &round_state,
                     const TableState &table_state, const PlayerState &player,
                     const ScoreResult &result, const int win_flag, const int han)
{
    if (han <= 0) {
        return 0.0;
    }
    const int score_limit = score_calculator_detail::get_score_title(result.fu, han);
    return score_calculator_detail::calc_score(
        player.seat_wind == Tile::East, win_flag & WinFlag::Tsumo, round_state.honba,
        table_state.kyotaku, table_config.game_mode, score_limit, han, result.fu)[0];
}

double score_without_bonus(const ExpectedScoreCalculator::Config &config,
                           const TableConfig &table_config,
                           const RoundState &round_state, const TableState &table_state,
                           const PlayerState &player, const SeparatedCount &hand_counts,
                           const SeparatedCount &wall_counts, const ScoreResult &result,
                           const int win_flag, const int removed_han)
{
    if (result.score_limit >= ScoreLimit::CountedYakuman) {
        return result.payments[0];
    }
    if (!config.enable_uradora || !(win_flag & WinFlag::Riichi) ||
        table_state.dora_indicators.empty()) {
        return score_for_han(table_config, round_state, table_state, player, result,
                             win_flag, result.han - removed_han);
    }

    const MergedCount wall = to_merged_count(wall_counts);
    MergedCount hand_and_melds = to_merged_count(hand_counts);
    for (const auto &meld : player.melds) {
        for (const auto tile : meld.tiles) {
            ++hand_and_melds[Tile::to_normal(tile)];
        }
    }
    const auto probabilities = calc_uradora_distribution(
        wall, hand_and_melds, table_state.dora_indicators.size(),
        table_config.game_mode);
    double score = 0.0;
    for (int ura = 0; ura <= 12; ++ura) {
        score += probabilities[ura] *
                 score_for_han(table_config, round_state, table_state, player, result,
                               win_flag, result.han + ura - removed_han);
    }
    return score;
}

void calc_shapley_allocation(const ExpectedScoreCalculator::Config &config,
                             const TableConfig &table_config,
                             const RoundState &round_state,
                             const TableState &table_state, const PlayerState &player,
                             const SeparatedCount &hand_counts,
                             const SeparatedCount &wall_counts,
                             const ScoreResult &result, const int win_flag,
                             ExpectedScoreCalculator::ScoreData &data)
{
    constexpr YakuFlags bonus =
        Yaku::Dora | Yaku::UraDora | Yaku::RedDora | Yaku::NukiDora;

    std::vector<YakuEntry> roles;
    roles.reserve(result.yaku_list.size() + 1);
    bool has_uradora_role = false;
    for (const auto &entry : result.yaku_list) {
        if (entry.han <= 0 && entry.yaku != Yaku::NagashiMangan) {
            continue;
        }
        roles.push_back(entry);
        has_uradora_role |= entry.yaku == Yaku::UraDora;
    }

    const bool probabilistic_uradora = config.enable_uradora &&
                                       (win_flag & WinFlag::Riichi) &&
                                       !table_state.dora_indicators.empty() &&
                                       result.score_limit < ScoreLimit::CountedYakuman;
    if (probabilistic_uradora && !has_uradora_role) {
        roles.push_back(YakuEntry{Yaku::UraDora, 0});
    }

    if (roles.empty()) {
        return;
    }
    if (roles.size() >= 63) {
        throw std::overflow_error("too many roles for exact Shapley calculation");
    }

    std::array<double, 13> uradora_probabilities{};
    if (probabilistic_uradora) {
        const MergedCount wall = to_merged_count(wall_counts);
        MergedCount hand_and_melds = to_merged_count(hand_counts);
        for (const auto &meld : player.melds) {
            for (const auto tile : meld.tiles) {
                ++hand_and_melds[Tile::to_normal(tile)];
            }
        }
        uradora_probabilities = calc_uradora_distribution(
            wall, hand_and_melds, table_state.dora_indicators.size(),
            table_config.game_mode);
    }
    else {
        uradora_probabilities[0] = 1.0;
    }

    const std::size_t role_count = roles.size();
    const std::uint64_t coalition_count = std::uint64_t{1} << role_count;
    std::vector<double> coalition_scores(coalition_count, 0.0);
    const bool yakuman = result.score_limit >= ScoreLimit::Yakuman;
    const bool nagashi = !roles.empty() && roles.front().yaku == Yaku::NagashiMangan;

    for (std::uint64_t coalition = 1; coalition < coalition_count; ++coalition) {
        if (nagashi) {
            coalition_scores[coalition] = result.payments[0];
            continue;
        }

        int han_or_multiplier = 0;
        bool has_value_role = false;
        bool include_uradora = false;
        for (std::size_t i = 0; i < role_count; ++i) {
            if (!(coalition & (std::uint64_t{1} << i))) {
                continue;
            }
            const auto &role = roles[i];
            han_or_multiplier += role.han;
            include_uradora |= role.yaku == Yaku::UraDora;
            has_value_role |= !(role.yaku & bonus) && role.han > 0;
        }

        if (yakuman) {
            if (han_or_multiplier > 0) {
                const int score_limit =
                    score_calculator_detail::get_score_title(han_or_multiplier);
                coalition_scores[coalition] = score_calculator_detail::calc_score(
                    player.seat_wind == Tile::East, win_flag & WinFlag::Tsumo,
                    round_state.honba, table_state.kyotaku, table_config.game_mode,
                    score_limit)[0];
            }
            continue;
        }

        if (!has_value_role) {
            continue;
        }
        if (!include_uradora || !probabilistic_uradora) {
            coalition_scores[coalition] =
                score_for_han(table_config, round_state, table_state, player, result,
                              win_flag, han_or_multiplier);
            continue;
        }

        double expected_score = 0.0;
        for (int ura = 0; ura <= 12; ++ura) {
            expected_score +=
                uradora_probabilities[ura] *
                score_for_han(table_config, round_state, table_state, player, result,
                              win_flag, han_or_multiplier + ura);
        }
        coalition_scores[coalition] = expected_score;
    }

    const double full_value = coalition_scores.back();
    const double value_tolerance = 1e-10 * std::max(1.0, std::abs(data.score));
    if (std::abs(full_value - data.score) > value_tolerance) {
        throw std::logic_error("Shapley grand coalition does not match total score");
    }

    std::vector<double> subset_weights(role_count, 0.0);
    double combinations = 1.0;
    for (std::size_t subset_size = 0; subset_size < role_count; ++subset_size) {
        if (subset_size > 0) {
            combinations *= static_cast<double>(role_count - subset_size) /
                            static_cast<double>(subset_size);
        }
        subset_weights[subset_size] =
            1.0 / (static_cast<double>(role_count) * combinations);
    }

    for (std::size_t i = 0; i < role_count; ++i) {
        const std::uint64_t role_bit = std::uint64_t{1} << i;
        double allocation = 0.0;
        for (std::uint64_t coalition = 0; coalition < coalition_count; ++coalition) {
            if (coalition & role_bit) {
                continue;
            }
            allocation +=
                subset_weights[popcount(coalition)] *
                (coalition_scores[coalition | role_bit] - coalition_scores[coalition]);
        }
        data.shapley[first_tile(roles[i].yaku)] += allocation;
    }

    double allocated = 0.0;
    for (const double value : data.shapley) {
        allocated += value;
    }
    data.shapley[first_tile(roles.front().yaku)] += data.score - allocated;
}

YakuValueTable without_yaku_value(const YakuFlags yaku)
{
    YakuValueTable table = ScoreCalculator::default_yaku_value_table();
    for (auto &entry : table.yaku_han) {
        if (entry.yaku == yaku) {
            entry.closed_han = 0;
            entry.open_han = 0;
        }
    }
    for (auto &entry : table.yakuman_multipliers) {
        if (entry.yaku == yaku) {
            entry.multiplier = 0;
        }
    }
    return table;
}

ExpectedScoreCalculator::ScoreData
calc_score_variant(const ExpectedScoreCalculator::Config &config,
                   const TableConfig &table_config, const RoundState &round_state,
                   const TableState &table_state, PlayerState &player,
                   SeparatedCount &hand_counts, SeparatedCount &wall_counts,
                   const int shanten_type, const int win_tile, const int win_flag)
{
    ExpectedScoreCalculator::ScoreData data;
    const ScoreResult result =
        ScoreCalculator::calc_fast(table_config, round_state, table_state, player,
                                   win_tile, win_flag, shanten_type);
    if (!result.success || !has_value_yaku(result)) {
        return data;
    }

    data.score = final_score(config, table_config, round_state, table_state, player,
                             hand_counts, wall_counts, result, win_flag);
    if (!config.calc_yaku_stats && !config.calc_shapley_stats) {
        return data;
    }

    for (const auto &entry : result.yaku_list) {
        if (config.yaku_filter & entry.yaku) {
            data.occurrence[first_tile(entry.yaku)] = 1.0;
        }
    }

    constexpr YakuFlags bonus = Yaku::Dora | Yaku::RedDora | Yaku::NukiDora;
    if (config.calc_yaku_stats) {
        for (const auto &entry : result.yaku_list) {
            if (!(config.yaku_filter & entry.yaku)) {
                continue;
            }
            const int index = first_tile(entry.yaku);
            data.inclusive[index] = data.score;

            double score_without = 0.0;
            if (entry.yaku & bonus) {
                score_without = score_without_bonus(
                    config, table_config, round_state, table_state, player, hand_counts,
                    wall_counts, result, win_flag, entry.han);
            }
            else {
                const YakuValueTable value_table = without_yaku_value(entry.yaku);
                const ScoreResult removed = ScoreCalculator::calc_fast(
                    table_config, round_state, table_state, player, win_tile, win_flag,
                    shanten_type, value_table);
                if (removed.success && has_value_yaku(removed, entry.yaku)) {
                    score_without = final_score(config, table_config, round_state,
                                                table_state, player, hand_counts,
                                                wall_counts, removed, win_flag);
                }
            }
            data.marginal[index] = std::max(0.0, data.score - score_without);
        }

        if ((config.yaku_filter & Yaku::UraDora) && config.enable_uradora &&
            (win_flag & WinFlag::Riichi) && !table_state.dora_indicators.empty() &&
            result.score_limit < ScoreLimit::CountedYakuman) {
            const MergedCount wall = to_merged_count(wall_counts);
            MergedCount hand_and_melds = to_merged_count(hand_counts);
            for (const auto &meld : player.melds) {
                for (const auto tile : meld.tiles) {
                    ++hand_and_melds[Tile::to_normal(tile)];
                }
            }
            const auto probabilities = calc_uradora_distribution(
                wall, hand_and_melds, table_state.dora_indicators.size(),
                table_config.game_mode);
            const auto scores = ScoreCalculator::get_up_scores(
                table_config, round_state, table_state, player, result, win_flag, 12);
            double inclusive = 0.0;
            for (int ura = 1; ura <= 12; ++ura) {
                inclusive += probabilities[ura] * scores[ura];
            }
            const int index = first_tile(Yaku::UraDora);
            data.occurrence[index] = 1.0 - probabilities[0];
            data.inclusive[index] = inclusive;
            data.marginal[index] = std::max(0.0, data.score - scores[0]);
        }
    }

    if (config.calc_shapley_stats) {
        calc_shapley_allocation(config, table_config, round_state, table_state, player,
                                hand_counts, wall_counts, result, win_flag, data);
    }
    return data;
}

ExpectedScoreCalculator::ScoreData calc_score_data(
    const ExpectedScoreCalculator::Config &config, const TableConfig &table_config,
    const RoundState &round_state, const TableState &table_state, PlayerState &player,
    SeparatedCount &hand_counts, SeparatedCount &wall_counts, const int shanten_type,
    const int win_tile, const std::uint8_t riichi_state, const bool last_tile)
{
    const bool riichi = riichi_state != NoRiichi;
    const bool ippatsu = config.enable_turn_yaku && riichi_state == IppatsuEligible;
    int tsumo_flag = riichi ? (WinFlag::Tsumo | WinFlag::Riichi) : WinFlag::Tsumo;
    if (ippatsu) {
        tsumo_flag |= WinFlag::Ippatsu;
    }
    const bool has_remaining_tiles = config.remaining_tiles >= 0;
    const bool final_draw_is_self =
        has_remaining_tiles && config.remaining_tiles % 4 == 0;
    if (config.enable_turn_yaku && last_tile && has_remaining_tiles &&
        final_draw_is_self) {
        tsumo_flag |= WinFlag::UnderTheSea;
    }
    auto score = calc_score_variant(config, table_config, round_state, table_state,
                                    player, hand_counts, wall_counts, shanten_type,
                                    win_tile, tsumo_flag);
    const double effective_ron_rate = config.ron_rate;
    if (effective_ron_rate == 0.0) {
        return score;
    }

    int ron_flag = riichi ? WinFlag::Riichi : WinFlag::None;
    if (ippatsu) {
        ron_flag |= WinFlag::Ippatsu;
    }
    if (config.enable_turn_yaku && last_tile && has_remaining_tiles &&
        !final_draw_is_self) {
        ron_flag |= WinFlag::UnderTheRiver;
    }
    const auto ron =
        calc_score_variant(config, table_config, round_state, table_state, player,
                           hand_counts, wall_counts, shanten_type, win_tile, ron_flag);
    const double tsumo_rate = 1.0 - effective_ron_rate;
    score.score = tsumo_rate * score.score + effective_ron_rate * ron.score;
    for (int i = 0; i < Yaku::Length; ++i) {
        score.inclusive[i] =
            tsumo_rate * score.inclusive[i] +
            effective_ron_rate * ron.inclusive[i];
        score.occurrence[i] =
            tsumo_rate * score.occurrence[i] +
            effective_ron_rate * ron.occurrence[i];
        score.marginal[i] =
            tsumo_rate * score.marginal[i] +
            effective_ron_rate * ron.marginal[i];
        score.shapley[i] =
            tsumo_rate * score.shapley[i] +
            effective_ron_rate * ron.shapley[i];
    }
    return score;
}

int distance(const SeparatedCount &hand, const SeparatedCount &hand_org)
{
    int dist = 0;
    for (int i = 0; i < hand.size(); ++i) {
        dist += std::max(hand[i] - hand_org[i], 0);
    }

    return dist;
}

int64_t add_red5_flags(int64_t tiles)
{
    tiles |= (tiles & (1LL << Tile::Manzu5)) ? (1LL << Tile::RedManzu5) : 0;
    tiles |= (tiles & (1LL << Tile::Pinzu5)) ? (1LL << Tile::RedPinzu5) : 0;
    tiles |= (tiles & (1LL << Tile::Souzu5)) ? (1LL << Tile::RedSouzu5) : 0;
    return tiles;
}

std::tuple<int, std::vector<std::tuple<int, int>>>
get_necessary_tiles(const ExpectedScoreCalculator::Config &config,
                    const PlayerState &player, const MergedCount &wall,
                    const int game_mode)
{
    const auto [shanten_type, shanten, tiles] = NecessaryTileCalculator::select(
        player.hand, player.num_melds(), config.shanten_type, game_mode);

    std::vector<std::tuple<int, int>> necessary_tiles;
    necessary_tiles.reserve(tiles.size());
    for (const auto tile : tiles) {
        necessary_tiles.emplace_back(tile, wall[tile]);
    }

    return {shanten, necessary_tiles};
}

} // namespace

MergedCount create_wall(const TableConfig &table_config, const TableState &table_state,
                        const PlayerState &player, const bool enable_reddora)
{
    MergedCount wall{0}, melds{0}, indicators{0};
    const bool is_sanma = table_config.game_mode == GameMode::Sanma;

    for (auto tile : table_state.dora_indicators) {
        ++indicators[Tile::to_normal(tile)];
        if (Tile::is_red(tile)) {
            ++indicators[tile];
        }
    }

    for (const auto &meld : player.melds) {
        for (auto tile : meld.tiles) {
            ++melds[Tile::to_normal(tile)];
            if (Tile::is_red(tile)) {
                ++melds[tile];
            }
        }
    }

    for (int i = 0; i < 34; ++i) {
        if (is_sanma && Tile::is_sanma_disabled(i)) {
            continue;
        }
        wall[i] = 4 - (player.hand[i] + melds[i] + indicators[i]);
    }
    if (enable_reddora) {
        for (int i = 34; i < 37; ++i) {
            if (is_sanma && Tile::is_sanma_disabled(i)) {
                continue;
            }
            wall[i] = 1 - (player.hand[i] + melds[i] + indicators[i]);
        }
    }

    // Extracted north tiles (nuki dora) are removed from the wall.
    wall[Tile::North] -= player.nuki_count;

    return wall;
}

class ExpectedScoreCalculator::GraphBuilder
{
  public:
    GraphBuilder(const Config &config, const TableConfig &table_config,
                 const RoundState &round_state, const TableState &table_state,
                 PlayerState &player, SeparatedCount &hand_counts,
                 SeparatedCount &wall_counts, const SeparatedCount &hand_org,
                 const int shanten_org)
        : config_(config)
        , table_config_(table_config)
        , round_state_(round_state)
        , table_state_(table_state)
        , player_(player)
        , hand_counts_(hand_counts)
        , wall_counts_(wall_counts)
        , hand_org_(hand_org)
        , shanten_org_(shanten_org)
        , initial_meld_count_(player.num_melds())
        , initially_closed_(player.is_closed())
        , hand_key_(hand_counts, NoRiichi, config.state_tag)
        , hand_mask_(nonzero_mask(hand_counts))
        , wall_mask_(nonzero_mask(wall_counts))
    {
        hand_key_.melds = meld_signature(player_.melds);
    }

    Vertex draw_node(std::uint8_t riichi_state);
    Vertex discard_node(std::uint8_t riichi_state);

    Graph &graph()
    {
        return graph_;
    }
    const Cache &draw_cache() const
    {
        return cache1_;
    }
    const Cache &discard_cache() const
    {
        return cache2_;
    }
    const std::vector<Vertex> &draw_vertices() const
    {
        return draw_vertices_;
    }
    const std::vector<Vertex> &discard_vertices() const
    {
        return discard_vertices_;
    }
    const std::vector<std::pair<Vertex, Vertex>> &ippatsu_expiries() const
    {
        return ippatsu_expiries_;
    }
    const std::vector<CallOption> &call_options() const
    {
        return call_options_;
    }

    void draw_tile(const int tile)
    {
        if (hand_counts_[tile] >= hand_org_[tile]) {
            ++exchange_distance_;
        }
        if (wall_counts_[tile] == 1) {
            wall_mask_ &= ~(std::uint64_t{1} << tile);
        }
        if (hand_counts_[tile] == 0) {
            hand_mask_ |= std::uint64_t{1} << tile;
        }
        hand_key_.change_tile(tile, 1);
        draw(player_, hand_counts_, wall_counts_, tile);
    }

    void discard_tile(const int tile)
    {
        if (hand_counts_[tile] > hand_org_[tile]) {
            --exchange_distance_;
        }
        if (hand_counts_[tile] == 1) {
            hand_mask_ &= ~(std::uint64_t{1} << tile);
        }
        if (wall_counts_[tile] == 0) {
            wall_mask_ |= std::uint64_t{1} << tile;
        }
        hand_key_.change_tile(tile, -1);
        discard(player_, hand_counts_, wall_counts_, tile);
    }

  private:
    const Config &config_;
    const TableConfig &table_config_;
    const RoundState &round_state_;
    const TableState &table_state_;
    PlayerState &player_;
    SeparatedCount &hand_counts_;
    SeparatedCount &wall_counts_;
    const SeparatedCount &hand_org_;
    const int shanten_org_;
    const int initial_meld_count_;
    const bool initially_closed_;
    CacheKey hand_key_;
    std::uint64_t hand_mask_;
    std::uint64_t wall_mask_;
    int exchange_distance_ = 0;
    Graph graph_;
    Cache cache1_;
    Cache cache2_;
    std::vector<Vertex> draw_vertices_;
    std::vector<Vertex> discard_vertices_;
    std::vector<std::pair<Vertex, Vertex>> ippatsu_expiries_;
    std::vector<CallOption> call_options_;
    Vertex win_terminal_ = NoVertex;

    void build_call_options(Vertex source, int source_shanten);
    void add_call_option(Vertex source, int called_tile, int meld_type,
                         const std::vector<int> &own_tiles, bool chi,
                         int source_shanten);

    Vertex win_terminal()
    {
        if (win_terminal_ == NoVertex) {
            win_terminal_ = graph_.add_vertex();
        }
        return win_terminal_;
    }
};

void ExpectedScoreCalculator::GraphBuilder::add_call_option(
    const Vertex source, const int called_tile, const int meld_type,
    const std::vector<int> &own_tiles, const bool chi, const int source_shanten)
{
    if (chi && initially_closed_) {
        const int called = Tile::to_normal(called_tile);
        int sequence_start = called;
        for (const int tile : own_tiles) {
            sequence_start = std::min(sequence_start, Tile::to_normal(tile));
        }
        const bool ryanmen =
            (called == sequence_start && sequence_start % 9 != 6) ||
            (called == sequence_start + 2 && sequence_start % 9 != 0);
        if (ryanmen) {
            return;
        }
    }

    const int weight = wall_counts_[called_tile];
    if (weight <= 0) {
        return;
    }

    for (const int tile : own_tiles) {
        hand_key_.change_tile(tile, -1);
        --hand_counts_[tile];
        --player_.hand[Tile::to_normal(tile)];
        if (Tile::is_red(tile)) {
            --player_.hand[tile];
        }
    }
    --wall_counts_[called_tile];
    std::vector<int> meld_tiles = own_tiles;
    meld_tiles.push_back(called_tile);
    player_.melds.push_back(
        Meld{meld_type, meld_tiles, called_tile,
             chi ? SeatType::Kamicha : SeatType::Toimen});
    hand_key_.melds = meld_signature(player_.melds);
    hand_mask_ = nonzero_mask(hand_counts_);
    wall_mask_ = nonzero_mask(wall_counts_);

    const auto [post_call_type, post_call_shanten, post_call_discards] =
        UnnecessaryTileCalculator::calc(player_.hand, player_.num_melds(),
                                        config_.shanten_type,
                                        table_config_.game_mode);
    if (post_call_shanten < source_shanten) {
        const Vertex target = discard_node(NoRiichi);
        call_options_.push_back(
            CallOption{source, target, called_tile, weight, chi});
    }

    player_.melds.pop_back();
    ++wall_counts_[called_tile];
    for (const int tile : own_tiles) {
        hand_key_.change_tile(tile, 1);
        ++hand_counts_[tile];
        ++player_.hand[Tile::to_normal(tile)];
        if (Tile::is_red(tile)) {
            ++player_.hand[tile];
        }
    }
    hand_key_.melds = meld_signature(player_.melds);
    hand_mask_ = nonzero_mask(hand_counts_);
    wall_mask_ = nonzero_mask(wall_counts_);
}

void ExpectedScoreCalculator::GraphBuilder::build_call_options(
    const Vertex source, const int source_shanten)
{
    if (!config_.enable_calls || player_.num_melds() >= 4 ||
        player_.num_melds() > initial_meld_count_) {
        return;
    }

    const auto concrete_tile_types = [this](const int normal) {
        std::vector<int> tiles;
        for (int tile = 0; tile < 37; ++tile) {
            if (Tile::to_normal(tile) == normal && hand_counts_[tile] > 0) {
                tiles.push_back(tile);
            }
        }
        return tiles;
    };

    for (int called_tile = 0; called_tile < 37; ++called_tile) {
        if (wall_counts_[called_tile] <= 0) {
            continue;
        }
        const int called = Tile::to_normal(called_tile);

        const auto same = concrete_tile_types(called);
        for (std::size_t i = 0; i < same.size(); ++i) {
            for (std::size_t j = i; j < same.size(); ++j) {
                if (i == j && hand_counts_[same[i]] < 2) {
                    continue;
                }
                add_call_option(source, called_tile, MeldType::Pon,
                                {same[i], same[j]}, false, source_shanten);
            }
        }

        if (called >= Tile::Manzu1 && called <= Tile::Souzu9) {
            const int suit_start = called / 9 * 9;
            const int rank = called - suit_start;
            for (int sequence_start = rank - 2; sequence_start <= rank;
                 ++sequence_start) {
                if (sequence_start < 0 || sequence_start > 6) {
                    continue;
                }
                std::array<int, 2> needed{};
                int count = 0;
                for (int offset = 0; offset < 3; ++offset) {
                    const int normal = suit_start + sequence_start + offset;
                    if (normal != called) {
                        needed[count++] = normal;
                    }
                }
                const auto first = concrete_tile_types(needed[0]);
                const auto second = concrete_tile_types(needed[1]);
                for (const int a : first) {
                    for (const int b : second) {
                        add_call_option(source, called_tile, MeldType::Chi,
                                        {a, b}, true, source_shanten);
                    }
                }
            }
        }
    }
}

ExpectedScoreCalculator::Vertex
ExpectedScoreCalculator::GraphBuilder::draw_node(const std::uint8_t riichi_state)
{
    const CacheKey key = hand_key_.with_riichi_state(riichi_state);
    if (const auto itr = cache1_.find(key); itr != cache1_.end()) {
        return itr->second;
    }

    auto [type, shanten, wait] =
        NecessaryTileCalculator::calc(player_.hand, player_.num_melds(),
                                      config_.shanten_type, table_config_.game_mode);

    const bool can_extend_search =
        exchange_distance_ + shanten < shanten_org_ + config_.extra;
    const bool allow_tegawari =
        config_.enable_tegawari && riichi_state == NoRiichi && can_extend_search;
    wait = add_red5_flags(wait);

    const Vertex vertex = graph_.add_vertex();
    graph_[vertex].is_tenpai = shanten == 0;
    graph_[vertex].has_open_meld = !player_.is_closed();
    cache1_[key] = vertex;
    draw_vertices_.push_back(vertex);

    if (riichi_state == NoRiichi) {
        build_call_options(vertex, shanten);
    }

    // The upstream graph omits non-effective draws as self-loops. That is valid for
    // ordinary states, but a miss on the first draw after riichi must consume
    // ippatsu. Build the same-hand established-riichi state as that expiry target.
    if (config_.enable_turn_yaku && riichi_state == IppatsuEligible) {
        ippatsu_expiries_.emplace_back(vertex, draw_node(RiichiEstablished));
    }

    std::uint64_t candidates = allow_tegawari ? wall_mask_ : wall_mask_ & wait;
    while (candidates) {
        const int i = first_tile(candidates);
        candidates &= candidates - 1;
        const bool is_wait = wait & (1LL << i);

        {
            const int weight = wall_counts_[i];

            draw_tile(i);

            // After riichi, non-winning tiles are represented by the implicit
            // self-loop (or the ippatsu-expiry transition above). A winning draw
            // is terminal, so no post-riichi hand change or repeated riichi can
            // enter the graph.
            if (config_.enable_turn_yaku && riichi_state != NoRiichi && shanten == 0 &&
                is_wait) {
                const auto score = calc_score_data(
                    config_, table_config_, round_state_, table_state_, player_,
                    hand_counts_, wall_counts_, type, i, riichi_state, false);
                const auto last_score = calc_score_data(
                    config_, table_config_, round_state_, table_state_, player_,
                    hand_counts_, wall_counts_, type, i, riichi_state, true);
                graph_.add_edge(vertex, win_terminal(), weight, score, last_score);
                discard_tile(i);
                continue;
            }

            const Vertex target = discard_node(riichi_state);

            if (!graph_.has_edge(vertex, target)) {
                // 自摸前の時点で聴牌の場合、有効牌自摸後は和了形のため、点数計算を行う
                ScoreData score, last_score;
                if (shanten == 0 && is_wait) {
                    score = calc_score_data(config_, table_config_, round_state_,
                                            table_state_, player_, hand_counts_,
                                            wall_counts_, type, i, riichi_state, false);
                    if (config_.enable_turn_yaku) {
                        last_score = calc_score_data(
                            config_, table_config_, round_state_, table_state_, player_,
                            hand_counts_, wall_counts_, type, i, riichi_state, true);
                    }
                    else {
                        last_score = score;
                    }
                }
                graph_.add_edge(vertex, target, weight, score, last_score);
            }

            discard_tile(i);
        }
    }

    return vertex;
}

ExpectedScoreCalculator::Vertex
ExpectedScoreCalculator::GraphBuilder::discard_node(const std::uint8_t riichi_state)
{
    const CacheKey key = hand_key_.with_riichi_state(riichi_state);
    if (const auto itr = cache2_.find(key); itr != cache2_.end()) {
        return itr->second;
    }

    auto [type, shanten, disc] =
        UnnecessaryTileCalculator::calc(player_.hand, player_.num_melds(),
                                        config_.shanten_type, table_config_.game_mode);

    const bool can_extend_search =
        exchange_distance_ + shanten < shanten_org_ + config_.extra;
    const bool allow_shanten_down =
        config_.enable_shanten_down && riichi_state == NoRiichi && can_extend_search;
    disc = add_red5_flags(disc);

    const Vertex vertex = graph_.add_vertex();
    graph_[vertex].is_tenpai = shanten == 0;
    graph_[vertex].has_open_meld = !player_.is_closed();
    cache2_[key] = vertex;
    discard_vertices_.push_back(vertex);

    std::uint64_t candidates = allow_shanten_down ? hand_mask_ : hand_mask_ & disc;
    while (candidates) {
        const int i = first_tile(candidates);
        candidates &= candidates - 1;
        const bool is_disc = disc & (1LL << i);

        {
            std::uint8_t next_riichi_state = riichi_state;
            if (riichi_state == IppatsuEligible) {
                next_riichi_state = RiichiEstablished;
            }
            if (riichi_state == NoRiichi && config_.enable_riichi &&
                player_.is_closed() && shanten == 0 && is_disc) {
                next_riichi_state =
                    config_.enable_turn_yaku ? IppatsuEligible : RiichiEstablished;
            }

            discard_tile(i);

            const int weight = wall_counts_[i];
            const Vertex source = draw_node(next_riichi_state);

            draw_tile(i);

            if (!graph_.has_edge(source, vertex)) {
                // 打牌前の時点で向聴数が-1の場合、和了形のため、点数計算を行う
                ScoreData score, last_score;
                if (shanten == -1) {
                    score = calc_score_data(config_, table_config_, round_state_,
                                            table_state_, player_, hand_counts_,
                                            wall_counts_, type, i, riichi_state, false);
                    if (config_.enable_turn_yaku) {
                        last_score = calc_score_data(
                            config_, table_config_, round_state_, table_state_, player_,
                            hand_counts_, wall_counts_, type, i, riichi_state, true);
                    }
                    else {
                        last_score = score;
                    }
                }
                graph_.add_edge(source, vertex, weight, score, last_score);
            }
        }
    }

    return vertex;
}

/**
 * @brief Calculate the probability of tenpai, the probability of winning, and the expected score.
 *        https://github.com/nekobean/mahjong-cpp/wiki/%E8%81%B4%E7%89%8C%E7%A2%BA%E7%8E%87%E3%80%81%E5%92%8C%E4%BA%86%E7%A2%BA%E7%8E%87%E3%80%81%E7%82%B9%E6%95%B0%E6%9C%9F%E5%BE%85%E5%80%A4
 * @param config Configulation
 * @param graph Graph
 * @param cache1 List of draw node
 * @param cache2 List of discard node
 */
ExpectedScoreCalculator::EdgeCsr
ExpectedScoreCalculator::build_edge_csr(const Graph &graph)
{
    const std::size_t vertex_count = graph.num_vertices();
    const std::size_t edge_count = graph.edges.size();
    assert(vertex_count <= std::numeric_limits<std::uint32_t>::max());
    assert(edge_count <= std::numeric_limits<std::uint32_t>::max());

    EdgeCsr edge_csr;
    edge_csr.draw_edge_offsets.assign(vertex_count + 1, 0);
    edge_csr.selection_edge_offsets.assign(vertex_count + 1, 0);

    for (std::size_t vi = 0; vi < vertex_count; ++vi) {
        for (std::uint32_t edge = graph.first_out_edges[vi]; edge != Graph::NoEdge;
             edge = graph.edges[edge].next_out) {
            ++edge_csr.draw_edge_offsets[vi + 1];
        }
        for (std::uint32_t edge = graph.first_in_edges[vi]; edge != Graph::NoEdge;
             edge = graph.edges[edge].next_in) {
            ++edge_csr.selection_edge_offsets[vi + 1];
        }
    }

    for (std::size_t vi = 0; vi < vertex_count; ++vi) {
        edge_csr.draw_edge_offsets[vi + 1] += edge_csr.draw_edge_offsets[vi];
        edge_csr.selection_edge_offsets[vi + 1] += edge_csr.selection_edge_offsets[vi];
    }

    edge_csr.draw_edges.resize(edge_csr.draw_edge_offsets.back());
    edge_csr.selection_edges.resize(edge_csr.selection_edge_offsets.back());

    std::vector<std::uint32_t> draw_positions = edge_csr.draw_edge_offsets;
    std::vector<std::uint32_t> selection_positions = edge_csr.selection_edge_offsets;

    for (std::size_t vi = 0; vi < vertex_count; ++vi) {
        for (std::uint32_t ei = graph.first_out_edges[vi]; ei != Graph::NoEdge;
             ei = graph.edges[ei].next_out) {
            const EdgeData &edge = graph.edges[ei];
            edge_csr.draw_edges[draw_positions[vi]++] =
                DrawEdge{edge.target,
                         edge.weight,
                         edge.score,
                         edge.contribution_offset,
                         edge.contribution_count,
                         edge.last_score,
                         edge.last_contribution_offset,
                         edge.last_contribution_count};
        }
        for (std::uint32_t ei = graph.first_in_edges[vi]; ei != Graph::NoEdge;
             ei = graph.edges[ei].next_in) {
            const EdgeData &edge = graph.edges[ei];
            edge_csr.selection_edges[selection_positions[vi]++] =
                SelectionEdge{edge.source};
        }
    }
    return edge_csr;
}

void ExpectedScoreCalculator::calc_stats(const Config &config, Graph &graph,
                                         const std::vector<Vertex> &draw_vertices,
                                         const std::vector<Vertex> &discard_vertices,
                                         const std::vector<std::pair<Vertex, Vertex>>
                                             &ippatsu_expiries,
                                         const std::vector<CallOption> &call_options,
                                         const EdgeCsr &edge_csr,
                                         const std::vector<Vertex> &root_vertices,
                                         std::vector<Stat> &stats)
{
    const auto total_win_probability = [&config](const double tsumo_probability) {
        if (tsumo_probability <= 0.0 || config.ron_rate <= 0.0) {
            return tsumo_probability;
        }
        const double tsumo_share = 1.0 - config.ron_rate;
        if (tsumo_share <= 0.0) {
            return 1.0;
        }
        // Let x be the ron probability on each of the three opponent discards.
        // No ron before the next draw is y=(1-x)^3.  Choose y so that
        // (1-y) : y*p equals ron_rate : tsumo_share, where p is the original
        // one-player tsumo probability.  The resulting total hazard is p /
        // (tsumo_share + ron_rate*p).
        return tsumo_probability /
               (tsumo_share + config.ron_rate * tsumo_probability);
    };
    const auto expand_turn_value = [](double &value, const double tsumo_probability,
                                      const double total_probability,
                                      const double win_value) {
        if (total_probability <= tsumo_probability || tsumo_probability >= 1.0) {
            return;
        }
        const double miss_value =
            (value - tsumo_probability * win_value) /
            (1.0 - tsumo_probability);
        value = total_probability * win_value +
                (1.0 - total_probability) * miss_value;
    };

    assert(root_vertices.size() == stats.size());
    std::vector<std::vector<const CallOption *>> calls_by_source(
        graph.num_vertices());
    for (const auto &option : call_options) {
        calls_by_source[option.source].push_back(&option);
    }
    for (auto &stat : stats) {
        stat.tenpai_prob.assign(config.t_max + 1, 0.0);
        stat.win_prob.assign(config.t_max + 1, 0.0);
        stat.exp_score.assign(config.t_max + 1, 0.0);
        stat.call_prob.assign(config.t_max + 1, 0.0);
    }

    std::vector<double> turn_tsumo_probability(graph.num_vertices(), 0.0);
    std::vector<double> turn_win_score(graph.num_vertices(), 0.0);
    for (int t = config.t_max; t >= config.t_min; --t) {
        const bool last_turn = config.enable_turn_yaku && t + 1 == LastTurn;
        struct IppatsuStateSnapshot
        {
            Vertex source;
            VertexData source_state;
            VertexData expired_state;
            int outgoing_weight;
        };
        std::vector<IppatsuStateSnapshot> ippatsu_expiry_snapshot;
        ippatsu_expiry_snapshot.reserve(ippatsu_expiries.size());
        for (const auto &[source, expired] : ippatsu_expiries) {
            int outgoing_weight = 0;
            const std::size_t vi = static_cast<std::size_t>(source);
            for (std::uint32_t ei = edge_csr.draw_edge_offsets[vi];
                 ei < edge_csr.draw_edge_offsets[vi + 1]; ++ei) {
                outgoing_weight += edge_csr.draw_edges[ei].weight;
            }
            ippatsu_expiry_snapshot.push_back(IppatsuStateSnapshot{
                source, graph[source], graph[expired], outgoing_weight});
        }
        // draw node
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(draw_vertices.size());
            ++i) {
            const Vertex vertex = draw_vertices[i];
            VertexData &s1 = graph[vertex];
            turn_tsumo_probability[vertex] = 0.0;
            turn_win_score[vertex] = 0.0;
            if (t == config.t_max) {
                s1.call_prob = s1.has_open_meld ? 1.0 : 0.0;
                if (s1.is_tenpai) {
                    s1.tenpai_prob = 1.0;
                }
                continue;
            }

            const std::size_t vi = static_cast<std::size_t>(vertex);
            const double previous_tenpai_prob = s1.tenpai_prob;
            const double previous_win_prob = s1.win_prob;
            const double previous_exp_score = s1.exp_score;
            const double previous_call_prob = s1.call_prob;
            double tenpai_delta = 0.0;
            double win_delta = 0.0;
            double score_delta = 0.0;
            double call_delta = 0.0;
            int immediate_win_weight = 0;
            double immediate_win_score = 0.0;
            for (std::uint32_t ei = edge_csr.draw_edge_offsets[vi];
                ei < edge_csr.draw_edge_offsets[vi + 1]; ++ei) {
                const DrawEdge &edge = edge_csr.draw_edges[ei];
                const VertexData &s2 = graph[edge.target];

                double tenpai_prob = s2.tenpai_prob;
                double win_prob = s2.win_prob;
                double exp_score = s2.exp_score;
                double call_prob = s2.call_prob;
                const double edge_score = last_turn ? edge.last_score : edge.score;
                if (edge_score > 0.0) {
                    tenpai_prob = 1.0;
                    win_prob = 1.0;
                    exp_score = std::max(edge_score, exp_score);
                    call_prob = s1.has_open_meld ? 1.0 : call_prob;
                    immediate_win_weight += edge.weight;
                    immediate_win_score += edge.weight * exp_score;
                }

                tenpai_delta += edge.weight * (tenpai_prob - previous_tenpai_prob);
                win_delta += edge.weight * (win_prob - previous_win_prob);
                score_delta += edge.weight * (exp_score - previous_exp_score);
                call_delta += edge.weight * (call_prob - previous_call_prob);
            }

            s1.tenpai_prob = previous_tenpai_prob + tenpai_delta / (config.sum - t);
            if (s1.is_tenpai) {
                s1.tenpai_prob = 1.0;
            }
            s1.win_prob = previous_win_prob + win_delta / (config.sum - t);
            s1.exp_score = previous_exp_score + score_delta / (config.sum - t);
            s1.call_prob = previous_call_prob + call_delta / (config.sum - t);
            if (immediate_win_weight > 0) {
                turn_tsumo_probability[vertex] =
                    static_cast<double>(immediate_win_weight) / (config.sum - t);
                turn_win_score[vertex] =
                    immediate_win_score / immediate_win_weight;
            }
        }

        if (t != config.t_max) {
            const double denominator = config.sum - t;
            for (const auto &snapshot : ippatsu_expiry_snapshot) {
                const int miss_weight =
                    std::max(0, config.sum - t - snapshot.outgoing_weight);
                VertexData &state = graph[snapshot.source];
                state.tenpai_prob +=
                    miss_weight *
                    (snapshot.expired_state.tenpai_prob -
                     snapshot.source_state.tenpai_prob) /
                    denominator;
                state.win_prob +=
                    miss_weight *
                    (snapshot.expired_state.win_prob - snapshot.source_state.win_prob) /
                    denominator;
                state.exp_score +=
                    miss_weight *
                    (snapshot.expired_state.exp_score - snapshot.source_state.exp_score) /
                    denominator;
                state.call_prob +=
                    miss_weight *
                    (snapshot.expired_state.call_prob -
                     snapshot.source_state.call_prob) /
                    denominator;
            }

            for (const Vertex vertex : draw_vertices) {
                const double tsumo_probability = turn_tsumo_probability[vertex];
                if (tsumo_probability <= 0.0) {
                    continue;
                }
                const double total_probability =
                    total_win_probability(tsumo_probability);
                VertexData &state = graph[vertex];
                expand_turn_value(state.win_prob, tsumo_probability,
                                  total_probability, 1.0);
                expand_turn_value(state.exp_score, tsumo_probability,
                                  total_probability, turn_win_score[vertex]);
            }

            if (config.enable_other_win_stop) {
                const int hazard_turn = std::min(LastTurn, t + 1);
                const int table_turn = hazard_turn == LastTurn ? LastTurn - 1
                                                               : hazard_turn;
                const double survival = 1.0 - config.other_win_hazard[table_turn];
                for (const Vertex vertex : draw_vertices) {
                    if (!graph[vertex].is_tenpai) {
                        graph[vertex].tenpai_prob *= survival;
                    }
                    graph[vertex].win_prob *= survival;
                    graph[vertex].exp_score *= survival;
                    if (!graph[vertex].has_open_meld) {
                        graph[vertex].call_prob *= survival;
                    }
                }
            }

            if (config.enable_calls) {
                const auto apply_call_slot = [&](const bool allow_chi) {
                    for (const Vertex vertex : draw_vertices) {
                        const auto &options = calls_by_source[vertex];
                        if (options.empty()) {
                            continue;
                        }
                        std::array<const CallOption *, 37> best{};
                        for (const CallOption *option : options) {
                            if (option->chi && !allow_chi) {
                                continue;
                            }
                            const CallOption *&selected = best[option->tile];
                            if (selected == nullptr ||
                                graph[option->target].exp_score >
                                    graph[selected->target].exp_score) {
                                selected = option;
                            }
                        }
                        VertexData &state = graph[vertex];
                        const VertexData before = state;
                        for (const CallOption *option : best) {
                            if (option == nullptr) {
                                continue;
                            }
                            const VertexData &called = graph[option->target];
                            if (called.exp_score <= before.exp_score) {
                                continue;
                            }
                            const double probability = option->weight / denominator;
                            state.tenpai_prob += probability *
                                (called.tenpai_prob - before.tenpai_prob);
                            state.win_prob += probability *
                                (called.win_prob - before.win_prob);
                            state.exp_score += probability *
                                (called.exp_score - before.exp_score);
                            state.call_prob += probability * (1.0 - before.call_prob);
                        }
                    }
                };
                // Backward induction over the three opponent discards. Chi is
                // available only from kamicha; pon is available from all seats.
                apply_call_slot(true);
                apply_call_slot(false);
                apply_call_slot(false);
            }
        }

        // discard node
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(discard_vertices.size());
            ++i) {
            const Vertex vertex = discard_vertices[i];
            VertexData &s1 = graph[vertex];
            double best_tenpai_prob = 0.0;
            double best_win_prob = 0.0;
            double best_exp_score = 0.0;
            double best_call_prob = 0.0;

            const std::size_t vi = static_cast<std::size_t>(vertex);
            for (std::uint32_t ei = edge_csr.selection_edge_offsets[vi];
                 ei < edge_csr.selection_edge_offsets[vi + 1]; ++ei) {
                const SelectionEdge &edge = edge_csr.selection_edges[ei];
                const VertexData &s2 = graph[edge.source];
                if (s2.tenpai_prob > best_tenpai_prob) {
                    best_tenpai_prob = s2.tenpai_prob;
                }
                best_win_prob = std::max(best_win_prob, s2.win_prob);
                if (s2.exp_score > best_exp_score) {
                    best_exp_score = s2.exp_score;
                    best_call_prob = s2.call_prob;
                }
            }

            s1.tenpai_prob = best_tenpai_prob;
            s1.win_prob = best_win_prob;
            s1.exp_score = best_exp_score;
            s1.call_prob = best_call_prob;
        }

        for (std::size_t i = 0; i < root_vertices.size(); ++i) {
            const VertexData &root = graph[root_vertices[i]];
            stats[i].tenpai_prob[t] = root.tenpai_prob;
            stats[i].win_prob[t] = root.win_prob;
            stats[i].exp_score[t] = root.exp_score;
            stats[i].call_prob[t] = root.call_prob;
        }
    }

    if ((!config.calc_yaku_stats && !config.calc_shapley_stats) || stats.empty()) {
        return;
    }

    YakuFlags active_yaku = Yaku::None;
    for (const auto &entry : graph.contributions) {
        active_yaku |= entry.yaku;
    }
    active_yaku &= config.yaku_filter;

    while (active_yaku) {
        const int yaku_index = first_tile(active_yaku);
        const YakuFlags yaku = YakuFlags{1} << yaku_index;
        active_yaku &= active_yaku - 1;

        for (auto &stat : stats) {
            stat.yaku_stats.push_back(
                YakuStat{yaku, std::vector<double>(config.t_max + 1, 0.0),
                         std::vector<double>(config.t_max + 1, 0.0),
                         std::vector<double>(config.t_max + 1, 0.0),
                         std::vector<double>(config.t_max + 1, 0.0)});
        }
        const std::size_t output_index = stats.front().yaku_stats.size() - 1;

        std::vector<double> occurrence(graph.num_vertices(), 0.0);
        std::vector<double> inclusive(graph.num_vertices(), 0.0);
        std::vector<double> marginal(graph.num_vertices(), 0.0);
        std::vector<double> shapley(graph.num_vertices(), 0.0);
        for (auto &vertex : graph.vertices) {
            vertex.exp_score = 0.0;
        }

        std::vector<double> turn_tsumo_probability(graph.num_vertices(), 0.0);
        std::vector<double> turn_win_score(graph.num_vertices(), 0.0);
        std::vector<double> turn_win_occurrence(graph.num_vertices(), 0.0);
        std::vector<double> turn_win_inclusive(graph.num_vertices(), 0.0);
        std::vector<double> turn_win_marginal(graph.num_vertices(), 0.0);
        std::vector<double> turn_win_shapley(graph.num_vertices(), 0.0);

        for (int t = config.t_max; t >= config.t_min; --t) {
            const bool last_turn = config.enable_turn_yaku && t + 1 == LastTurn;
            struct ContributionSnapshot
            {
                Vertex source;
                double source_score;
                double source_occurrence;
                double source_inclusive;
                double source_marginal;
                double source_shapley;
                double expired_score;
                double expired_occurrence;
                double expired_inclusive;
                double expired_marginal;
                double expired_shapley;
                int outgoing_weight;
            };
            std::vector<ContributionSnapshot> ippatsu_expiry_snapshot;
            ippatsu_expiry_snapshot.reserve(ippatsu_expiries.size());
            for (const auto &[source, expired] : ippatsu_expiries) {
                int outgoing_weight = 0;
                const std::size_t vi = static_cast<std::size_t>(source);
                for (std::uint32_t ei = edge_csr.draw_edge_offsets[vi];
                     ei < edge_csr.draw_edge_offsets[vi + 1]; ++ei) {
                    outgoing_weight += edge_csr.draw_edges[ei].weight;
                }
                ippatsu_expiry_snapshot.push_back(ContributionSnapshot{
                    source, graph[source].exp_score, occurrence[source],
                    inclusive[source], marginal[source], shapley[source],
                    graph[expired].exp_score, occurrence[expired], inclusive[expired],
                    marginal[expired], shapley[expired], outgoing_weight});
            }
            if (t != config.t_max) {
                for (const Vertex vertex : draw_vertices) {
                    VertexData &state = graph[vertex];
                    turn_tsumo_probability[vertex] = 0.0;
                    turn_win_score[vertex] = 0.0;
                    turn_win_occurrence[vertex] = 0.0;
                    turn_win_inclusive[vertex] = 0.0;
                    turn_win_marginal[vertex] = 0.0;
                    turn_win_shapley[vertex] = 0.0;
                    const double previous_score = state.exp_score;
                    const double previous_occurrence = occurrence[vertex];
                    const double previous_inclusive = inclusive[vertex];
                    const double previous_marginal = marginal[vertex];
                    const double previous_shapley = shapley[vertex];
                    double score_delta = 0.0;
                    double occurrence_delta = 0.0;
                    double inclusive_delta = 0.0;
                    double marginal_delta = 0.0;
                    double shapley_delta = 0.0;
                    int immediate_win_weight = 0;
                    double immediate_win_score = 0.0;
                    double immediate_win_occurrence = 0.0;
                    double immediate_win_inclusive = 0.0;
                    double immediate_win_marginal = 0.0;
                    double immediate_win_shapley = 0.0;
                    const std::size_t vi = static_cast<std::size_t>(vertex);
                    for (std::uint32_t ei = edge_csr.draw_edge_offsets[vi];
                        ei < edge_csr.draw_edge_offsets[vi + 1]; ++ei) {
                        const DrawEdge &edge = edge_csr.draw_edges[ei];
                        const double continuation_score = graph[edge.target].exp_score;
                        double selected_score = continuation_score;
                        double selected_occurrence = occurrence[edge.target];
                        double selected_inclusive = inclusive[edge.target];
                        double selected_marginal = marginal[edge.target];
                        double selected_shapley = shapley[edge.target];

                        const double edge_score =
                            last_turn ? edge.last_score : edge.score;
                        const std::uint32_t contribution_offset =
                            last_turn ? edge.last_contribution_offset
                                      : edge.contribution_offset;
                        const std::uint16_t contribution_count =
                            last_turn ? edge.last_contribution_count
                                      : edge.contribution_count;
                        if (edge_score > 0.0) {
                            selected_score = std::max(edge_score, continuation_score);
                            if (edge_score >= continuation_score) {
                                selected_inclusive = 0.0;
                                selected_occurrence = 0.0;
                                selected_marginal = 0.0;
                                selected_shapley = 0.0;
                                for (std::uint32_t ci = contribution_offset;
                                     ci < contribution_offset + contribution_count;
                                     ++ci) {
                                    const ContributionData &entry =
                                        graph.contributions[ci];
                                    if (entry.yaku == yaku) {
                                        selected_occurrence = entry.occurrence;
                                        selected_inclusive = entry.inclusive;
                                        selected_marginal = entry.marginal;
                                        selected_shapley = entry.shapley;
                                        break;
                                    }
                                }
                            }
                            immediate_win_weight += edge.weight;
                            immediate_win_score += edge.weight * selected_score;
                            immediate_win_occurrence +=
                                edge.weight * selected_occurrence;
                            immediate_win_inclusive +=
                                edge.weight * selected_inclusive;
                            immediate_win_marginal +=
                                edge.weight * selected_marginal;
                            immediate_win_shapley +=
                                edge.weight * selected_shapley;
                        }
                        score_delta += edge.weight * (selected_score - previous_score);
                        occurrence_delta +=
                            edge.weight * (selected_occurrence - previous_occurrence);
                        inclusive_delta +=
                            edge.weight * (selected_inclusive - previous_inclusive);
                        marginal_delta +=
                            edge.weight * (selected_marginal - previous_marginal);
                        shapley_delta +=
                            edge.weight * (selected_shapley - previous_shapley);
                    }
                    const double denominator = config.sum - t;
                    state.exp_score = previous_score + score_delta / denominator;
                    occurrence[vertex] =
                        previous_occurrence + occurrence_delta / denominator;
                    inclusive[vertex] =
                        previous_inclusive + inclusive_delta / denominator;
                    marginal[vertex] = previous_marginal + marginal_delta / denominator;
                    shapley[vertex] = previous_shapley + shapley_delta / denominator;
                    if (immediate_win_weight > 0) {
                        turn_tsumo_probability[vertex] =
                            static_cast<double>(immediate_win_weight) / denominator;
                        turn_win_score[vertex] =
                            immediate_win_score / immediate_win_weight;
                        turn_win_occurrence[vertex] =
                            immediate_win_occurrence / immediate_win_weight;
                        turn_win_inclusive[vertex] =
                            immediate_win_inclusive / immediate_win_weight;
                        turn_win_marginal[vertex] =
                            immediate_win_marginal / immediate_win_weight;
                        turn_win_shapley[vertex] =
                            immediate_win_shapley / immediate_win_weight;
                    }
                }

                const double denominator = config.sum - t;
                for (const auto &snapshot : ippatsu_expiry_snapshot) {
                    const int miss_weight =
                        std::max(0, config.sum - t - snapshot.outgoing_weight);
                    graph[snapshot.source].exp_score +=
                        miss_weight * (snapshot.expired_score - snapshot.source_score) /
                        denominator;
                    occurrence[snapshot.source] +=
                        miss_weight *
                        (snapshot.expired_occurrence - snapshot.source_occurrence) /
                        denominator;
                    inclusive[snapshot.source] +=
                        miss_weight *
                        (snapshot.expired_inclusive - snapshot.source_inclusive) /
                        denominator;
                    marginal[snapshot.source] +=
                        miss_weight *
                        (snapshot.expired_marginal - snapshot.source_marginal) /
                        denominator;
                    shapley[snapshot.source] +=
                        miss_weight *
                        (snapshot.expired_shapley - snapshot.source_shapley) /
                        denominator;
                }

                for (const Vertex vertex : draw_vertices) {
                    const double tsumo_probability = turn_tsumo_probability[vertex];
                    if (tsumo_probability <= 0.0) {
                        continue;
                    }
                    const double total_probability =
                        total_win_probability(tsumo_probability);
                    expand_turn_value(graph[vertex].exp_score, tsumo_probability,
                                      total_probability, turn_win_score[vertex]);
                    expand_turn_value(occurrence[vertex], tsumo_probability,
                                      total_probability,
                                      turn_win_occurrence[vertex]);
                    expand_turn_value(inclusive[vertex], tsumo_probability,
                                      total_probability,
                                      turn_win_inclusive[vertex]);
                    expand_turn_value(marginal[vertex], tsumo_probability,
                                      total_probability,
                                      turn_win_marginal[vertex]);
                    expand_turn_value(shapley[vertex], tsumo_probability,
                                      total_probability, turn_win_shapley[vertex]);
                }


                if (config.enable_other_win_stop) {
                    const int hazard_turn = std::min(LastTurn, t + 1);
                    const int table_turn = hazard_turn == LastTurn ? LastTurn - 1
                                                                   : hazard_turn;
                    const double survival =
                        1.0 - config.other_win_hazard[table_turn];
                    for (const Vertex vertex : draw_vertices) {
                        graph[vertex].exp_score *= survival;
                        occurrence[vertex] *= survival;
                        inclusive[vertex] *= survival;
                        marginal[vertex] *= survival;
                        shapley[vertex] *= survival;
                    }
                }

                if (config.enable_calls) {
                    const auto apply_call_slot = [&](const bool allow_chi) {
                        for (const Vertex vertex : draw_vertices) {
                            const auto &options = calls_by_source[vertex];
                            if (options.empty()) {
                                continue;
                            }
                            std::array<const CallOption *, 37> best{};
                            for (const CallOption *option : options) {
                                if (option->chi && !allow_chi) {
                                    continue;
                                }
                                const CallOption *&selected = best[option->tile];
                                if (selected == nullptr ||
                                    graph[option->target].exp_score >
                                        graph[selected->target].exp_score) {
                                    selected = option;
                                }
                            }
                            const double before_score = graph[vertex].exp_score;
                            const double before_occurrence = occurrence[vertex];
                            const double before_inclusive = inclusive[vertex];
                            const double before_marginal = marginal[vertex];
                            const double before_shapley = shapley[vertex];
                            for (const CallOption *option : best) {
                                if (option == nullptr ||
                                    graph[option->target].exp_score <= before_score) {
                                    continue;
                                }
                                const double probability =
                                    option->weight / denominator;
                                const Vertex target = option->target;
                                graph[vertex].exp_score += probability *
                                    (graph[target].exp_score - before_score);
                                occurrence[vertex] += probability *
                                    (occurrence[target] - before_occurrence);
                                inclusive[vertex] += probability *
                                    (inclusive[target] - before_inclusive);
                                marginal[vertex] += probability *
                                    (marginal[target] - before_marginal);
                                shapley[vertex] += probability *
                                    (shapley[target] - before_shapley);
                            }
                        }
                    };
                    apply_call_slot(true);
                    apply_call_slot(false);
                    apply_call_slot(false);
                }
            }

            for (const Vertex vertex : discard_vertices) {
                double best_score = 0.0;
                double best_occurrence = 0.0;
                double best_inclusive = 0.0;
                double best_marginal = 0.0;
                double best_shapley = 0.0;
                const std::size_t vi = static_cast<std::size_t>(vertex);
                for (std::uint32_t ei = edge_csr.selection_edge_offsets[vi];
                     ei < edge_csr.selection_edge_offsets[vi + 1]; ++ei) {
                    const Vertex source = edge_csr.selection_edges[ei].source;
                    if (graph[source].exp_score > best_score) {
                        best_score = graph[source].exp_score;
                        best_occurrence = occurrence[source];
                        best_inclusive = inclusive[source];
                        best_marginal = marginal[source];
                        best_shapley = shapley[source];
                    }
                }
                graph[vertex].exp_score = best_score;
                occurrence[vertex] = best_occurrence;
                inclusive[vertex] = best_inclusive;
                marginal[vertex] = best_marginal;
                shapley[vertex] = best_shapley;
            }

            for (std::size_t i = 0; i < root_vertices.size(); ++i) {
                stats[i].yaku_stats[output_index].occurrence_prob[t] =
                    occurrence[root_vertices[i]];
                stats[i].yaku_stats[output_index].inclusive_score[t] =
                    inclusive[root_vertices[i]];
                stats[i].yaku_stats[output_index].marginal_score[t] =
                    marginal[root_vertices[i]];
                stats[i].yaku_stats[output_index].shapley_score[t] =
                    shapley[root_vertices[i]];
            }
        }
    }
}

std::tuple<std::vector<ExpectedScoreCalculator::Stat>, int>
ExpectedScoreCalculator::calc(const Config &config, const TableConfig &table_config,
                              const RoundState &round_state,
                              const TableState &table_state, const PlayerState &player)
{
    const MergedCount wall =
        create_wall(table_config, table_state, player, config.enable_reddora);

    return calc(config, table_config, round_state, table_state, player, wall);
}

void ExpectedScoreCalculator::calc_draw_hand(
    const Config &config, const PlayerState &player, const TableConfig &table_config,
    const RoundState &round_state, const TableState &table_state,
    const MergedCount &wall, const SeparatedCount &hand_counts,
    GraphBuilder &graph_builder, std::vector<Stat> &stats)
{
    // 13枚の場合は自摸を起点に手牌遷移のグラフを作成する。
    const Vertex vertex = graph_builder.draw_node(NoRiichi);
    stats.emplace_back(Stat{Tile::Null, {}, {}, {}, {}, {}, 0, {}});

    // 確率、期待値を計算する。
    const EdgeCsr edge_csr = build_edge_csr(graph_builder.graph());
    calc_stats(config, graph_builder.graph(), graph_builder.draw_vertices(),
               graph_builder.discard_vertices(), graph_builder.ippatsu_expiries(),
               graph_builder.call_options(), edge_csr, {vertex}, stats);

    // 結果を取得する。
    const VertexData &state = graph_builder.graph()[vertex];

    // 有効牌の一覧を計算する。
    const auto [shanten2, necessary_tiles] =
        get_necessary_tiles(config, player, wall, table_config.game_mode);

    stats.front().necessary_tiles = necessary_tiles;
    stats.front().shanten = shanten2;
}

void ExpectedScoreCalculator::calc_discard_hand(
    const Config &config, PlayerState &player, const TableConfig &table_config,
    const RoundState &round_state, const TableState &table_state,
    const MergedCount &wall, SeparatedCount &hand_counts, SeparatedCount &wall_counts,
    GraphBuilder &graph_builder, std::vector<Stat> &stats)
{
    // 14枚の場合は打牌を起点に手牌遷移のグラフを作成する。
    graph_builder.discard_node(NoRiichi);

    // 確率、期待値を計算する。
    // 結果を取得する。
    auto [discard_type, discard_shanten, discard_tiles] =
        UnnecessaryTileCalculator::calc(player.hand, player.num_melds(),
                                        config.shanten_type, table_config.game_mode);
    discard_tiles = add_red5_flags(discard_tiles);

    std::vector<Vertex> root_vertices;
    std::uint64_t discard_candidates = nonzero_mask(hand_counts);
    while (discard_candidates) {
        const int i = first_tile(discard_candidates);
        discard_candidates &= discard_candidates - 1;
        if (hand_counts[i] > 0) {
            const bool is_disc = discard_tiles & (1LL << i);
            const bool call_riichi = config.enable_riichi && player.is_closed() &&
                                     discard_shanten == 0 && is_disc;
            const std::uint8_t riichi_state =
                call_riichi
                    ? (config.enable_turn_yaku ? IppatsuEligible : RiichiEstablished)
                    : NoRiichi;

            discard(player, hand_counts, wall_counts, i);
            auto root_key = CacheKey(hand_counts, riichi_state, config.state_tag);
            root_key.melds = meld_signature(player.melds);
            if (const auto itr = graph_builder.draw_cache().find(root_key);
                itr != graph_builder.draw_cache().end()) {
                const auto [shanten2, necessary_tiles] =
                    get_necessary_tiles(config, player, wall, table_config.game_mode);

                root_vertices.push_back(itr->second);
                stats.emplace_back(
                    Stat{i, {}, {}, {}, {}, necessary_tiles, shanten2, {}});
            }
            draw(player, hand_counts, wall_counts, i);
        }
    }

    const EdgeCsr edge_csr = build_edge_csr(graph_builder.graph());
    calc_stats(config, graph_builder.graph(), graph_builder.draw_vertices(),
               graph_builder.discard_vertices(), graph_builder.ippatsu_expiries(),
               graph_builder.call_options(), edge_csr, root_vertices, stats);
}

namespace
{
const ExpectedScoreCalculator::YakuStat *
find_yaku_stat(const ExpectedScoreCalculator::Stat &stat, const YakuFlags yaku)
{
    const auto found =
        std::find_if(stat.yaku_stats.begin(), stat.yaku_stats.end(),
                     [yaku](const auto &entry) { return entry.yaku == yaku; });
    return found == stat.yaku_stats.end() ? nullptr : &*found;
}

ExpectedScoreCalculator::YakuStat &
ensure_yaku_stat(ExpectedScoreCalculator::Stat &stat, const YakuFlags yaku,
                 const std::size_t size)
{
    const auto found =
        std::find_if(stat.yaku_stats.begin(), stat.yaku_stats.end(),
                     [yaku](const auto &entry) { return entry.yaku == yaku; });
    if (found != stat.yaku_stats.end()) {
        return *found;
    }
    stat.yaku_stats.push_back(ExpectedScoreCalculator::YakuStat{
        yaku, std::vector<double>(size, 0.0), std::vector<double>(size, 0.0),
        std::vector<double>(size, 0.0), std::vector<double>(size, 0.0)});
    return stat.yaku_stats.back();
}

double at_or_zero(const std::vector<double> *values, const std::size_t i)
{
    return values && i < values->size() ? (*values)[i] : 0.0;
}

bool is_turn_limited_yaku(const YakuFlags yaku)
{
    return yaku == Yaku::Ippatsu || yaku == Yaku::UnderTheSea ||
           yaku == Yaku::UnderTheRiver;
}

void merge_turn_yaku_overlay(
    std::vector<ExpectedScoreCalculator::Stat> &base,
    const std::vector<ExpectedScoreCalculator::Stat> &turn_on,
    const std::vector<ExpectedScoreCalculator::Stat> &turn_off)
{
    for (auto &base_stat : base) {
        const auto on_it = std::find_if(
            turn_on.begin(), turn_on.end(), [&](const auto &stat) {
                return stat.tile == base_stat.tile;
            });
        const auto off_it = std::find_if(
            turn_off.begin(), turn_off.end(), [&](const auto &stat) {
                return stat.tile == base_stat.tile;
            });
        if (on_it == turn_on.end() || off_it == turn_off.end()) {
            throw std::logic_error("Turn-yaku overlay discard set mismatch");
        }

        const std::size_t value_count = base_stat.exp_score.size();
        for (std::size_t i = 0; i < value_count; ++i) {
            base_stat.exp_score[i] += on_it->exp_score[i] - off_it->exp_score[i];
        }

        YakuFlags active_yaku = Yaku::None;
        for (const auto &entry : base_stat.yaku_stats) {
            active_yaku |= entry.yaku;
        }
        for (const auto &entry : on_it->yaku_stats) {
            active_yaku |= entry.yaku;
        }
        for (const auto &entry : off_it->yaku_stats) {
            active_yaku |= entry.yaku;
        }

        while (active_yaku) {
            const int index = first_tile(active_yaku);
            const YakuFlags yaku = YakuFlags{1} << index;
            active_yaku &= active_yaku - 1;
            auto &target = ensure_yaku_stat(base_stat, yaku, value_count);
            const auto *on = find_yaku_stat(*on_it, yaku);
            const auto *off = find_yaku_stat(*off_it, yaku);

            for (std::size_t i = 0; i < value_count; ++i) {
                target.inclusive_score[i] +=
                    at_or_zero(on ? &on->inclusive_score : nullptr, i) -
                    at_or_zero(off ? &off->inclusive_score : nullptr, i);
                target.marginal_score[i] +=
                    at_or_zero(on ? &on->marginal_score : nullptr, i) -
                    at_or_zero(off ? &off->marginal_score : nullptr, i);
                target.shapley_score[i] +=
                    at_or_zero(on ? &on->shapley_score : nullptr, i) -
                    at_or_zero(off ? &off->shapley_score : nullptr, i);
                if (is_turn_limited_yaku(yaku)) {
                    target.occurrence_prob[i] =
                        at_or_zero(on ? &on->occurrence_prob : nullptr, i);
                }
            }
        }
        std::sort(base_stat.yaku_stats.begin(), base_stat.yaku_stats.end(),
                  [](const auto &lhs, const auto &rhs) {
                      return lhs.yaku < rhs.yaku;
                  });
    }
}
} // namespace

std::tuple<std::vector<ExpectedScoreCalculator::Stat>, int>
ExpectedScoreCalculator::calc(const Config &config, const TableConfig &table_config,
                              const RoundState &round_state,
                              const TableState &table_state,
                              const PlayerState &player, const MergedCount &wall)
{
    if (!config.enable_turn_yaku || !config.enable_tegawari || !config.calc_stats) {
        return calc_core(config, table_config, round_state, table_state, player, wall);
    }

    Config base_config = config;
    base_config.enable_turn_yaku = false;
    auto [base, base_searched] =
        calc_core(base_config, table_config, round_state, table_state, player, wall);

    Config overlay_on = config;
    overlay_on.enable_tegawari = false;
    auto [turn_on, on_searched] =
        calc_core(overlay_on, table_config, round_state, table_state, player, wall);

    Config overlay_off = overlay_on;
    overlay_off.enable_turn_yaku = false;
    auto [turn_off, off_searched] =
        calc_core(overlay_off, table_config, round_state, table_state, player, wall);

    merge_turn_yaku_overlay(base, turn_on, turn_off);
    return {std::move(base), base_searched + on_searched + off_searched};
}

std::tuple<std::vector<ExpectedScoreCalculator::Stat>, int>
ExpectedScoreCalculator::calc_core(const Config &_config,
                                   const TableConfig &_table_config,
                                   const RoundState &_round_state,
                                   const TableState &_table_state,
                                   const PlayerState &_player,
                                   const MergedCount &_wall)
{
    Config config = _config;
    TableConfig table_config = _table_config;
    RoundState round_state = _round_state;
    TableState table_state = _table_state;
    PlayerState player = _player;
    MergedCount wall = _wall;
    assert(config.t_min >= 0);
    assert(config.t_max <= MaxTurn);
    if (config.ron_rate < 0.0 || config.ron_rate > 1.0) {
        throw std::invalid_argument("ron_rate must be in [0, 1].");
    }
    if (config.remaining_tiles < -1 || config.remaining_tiles > 70) {
        throw std::invalid_argument("remaining_tiles must be in [-1, 70].");
    }
    for (int turn = 1; turn <= LastTurn; ++turn) {
        if (config.other_win_hazard[turn] < 0.0 ||
            config.other_win_hazard[turn] > 1.0) {
            throw std::invalid_argument(
                "other_win_hazard values must be in [0, 1].");
        }
    }

    if (!config.enable_reddora) {
        normalize_red_fives(table_config, table_state);
        normalize_red_fives(player);
        normalize_red_fives(wall);
    }

    if (config.sum == 0) {
        config.sum = std::accumulate(wall.begin(), wall.begin() + 34, 0);
    }

    SeparatedCount hand_counts = to_separated_count(player.hand);
    SeparatedCount wall_counts = to_separated_count(wall);
    std::vector<Stat> stats;
    const int num_tiles = player.num_tiles() + player.num_melds() * 3;

    if (!config.calc_stats) {
        if (num_tiles == 13) {
            const auto [shanten, necessary_tiles] =
                get_necessary_tiles(config, player, wall, table_config.game_mode);
            stats.emplace_back(
                Stat{Tile::Null, {}, {}, {}, {}, necessary_tiles, shanten, {}});
        }
        else {
            std::uint64_t discard_candidates = nonzero_mask(hand_counts);
            while (discard_candidates) {
                const int i = first_tile(discard_candidates);
                discard_candidates &= discard_candidates - 1;
                if (hand_counts[i] > 0) {
                    discard(player, hand_counts, wall_counts, i);
                    const auto [shanten, necessary_tiles] = get_necessary_tiles(
                        config, player, wall, table_config.game_mode);
                    stats.emplace_back(
                        Stat{i, {}, {}, {}, {}, necessary_tiles, shanten, {}});
                    draw(player, hand_counts, wall_counts, i);
                }
            }
        }

        return {stats, 0};
    }

    const SeparatedCount hand_org = hand_counts;
    const int shanten_org = std::get<1>(ShantenCalculator::calc(
        player.hand, player.num_melds(), config.shanten_type, table_config.game_mode));
    GraphBuilder graph_builder(config, table_config, round_state, table_state, player,
                               hand_counts, wall_counts, hand_org, shanten_org);

    if (num_tiles == 13) {
        calc_draw_hand(config, player, table_config, round_state, table_state, wall,
                       hand_counts, graph_builder, stats);
    }
    else {
        calc_discard_hand(config, player, table_config, round_state, table_state, wall,
                          hand_counts, wall_counts, graph_builder, stats);
    }

    const int searched = static_cast<int>(graph_builder.graph().num_vertices());

    return {stats, searched};
}

} // namespace mahjong
