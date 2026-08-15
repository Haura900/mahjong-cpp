#include "request_processor.hpp"

#include <chrono>
#include <algorithm>
#include <numeric>
#include <stdexcept>

using namespace mahjong;

CalculationResult calculate_result(const Request &req)
{
    CalculationResult result;

    result.config = req.config;
    if (result.config.enable_situational_hazard) {
        double multiplier = 1.0;
        if (result.config.opponent_riichi_count >= 2) {
            multiplier *= result.config.opponent_double_riichi_multiplier;
        }
        else if (result.config.opponent_riichi_count == 1) {
            multiplier *= result.config.opponent_riichi_multiplier;
        }
        for (int i = 0; i < result.config.opponent_two_meld_count; ++i) {
            multiplier *= result.config.opponent_two_meld_multiplier;
        }
        if (result.config.self_riichi) {
            multiplier *= result.config.self_riichi_multiplier;
        }
        for (int turn = 1; turn <= 18; ++turn) {
            result.config.other_win_hazard[turn] = std::clamp(
                result.config.other_win_hazard[turn] * multiplier, 0.0, 1.0);
        }
        result.config.other_win_hazard[18] =
            result.config.other_win_hazard[17];
    }
    result.config.sum = std::accumulate(req.wall.begin(), req.wall.begin() + 34, 0);
    result.config.shanten_type = ShantenFlag::All;
    result.shanten = std::get<1>(
        ShantenCalculator::calc(req.player.hand, req.player.num_melds(),
                                ShantenFlag::All, req.table_config.game_mode));
    result.regular_shanten = std::get<1>(
        ShantenCalculator::calc(req.player.hand, req.player.num_melds(),
                                ShantenFlag::StandardHand, req.table_config.game_mode));
    result.seven_pairs_shanten = std::get<1>(
        ShantenCalculator::calc(req.player.hand, req.player.num_melds(),
                                ShantenFlag::SevenPairs, req.table_config.game_mode));
    result.thirteen_orphans_shanten = std::get<1>(ShantenCalculator::calc(
        req.player.hand, req.player.num_melds(), ShantenFlag::ThirteenOrphans,
        req.table_config.game_mode));
    if (result.config.auto_disable_deep_search && result.shanten >= 4) {
        result.config.enable_shanten_down = false;
        result.config.enable_tegawari = false;
    }
    if (!req.calc_stats_explicit) {
        result.config.calc_stats = result.shanten <= 3;
    }
    if (result.shanten == -1) {
        throw std::runtime_error(u8"手牌はすでに和了形です。");
    }

    const auto start = std::chrono::steady_clock::now();
    std::tie(result.stats, result.searched) =
        ExpectedScoreCalculator::calc(result.config, req.table_config, req.round_state,
                                      req.table_state, req.player, req.wall);
    if (result.config.enable_ev_breakdown) {
        for (auto &stat : result.stats) {
            const std::size_t size = stat.exp_score.size();
            stat.win_ev = stat.exp_score;
            stat.deal_in_ev.assign(size, 0.0);
            stat.tenpai_ev.assign(size, 0.0);
            stat.total_ev.assign(size, 0.0);
            const bool discard = stat.tile >= 0 && stat.tile < 37;
            const double deal_in = discard
                ? -result.config.deal_in_probability[stat.tile] *
                      result.config.deal_in_value[stat.tile]
                : 0.0;
            for (std::size_t turn = 0; turn < size; ++turn) {
                stat.deal_in_ev[turn] = deal_in;
                const double exhaustive_tenpai_probability = std::clamp(
                    stat.tenpai_prob[turn] - stat.win_prob[turn], 0.0, 1.0);
                stat.tenpai_ev[turn] = exhaustive_tenpai_probability *
                                       result.config.tenpai_payment;
                stat.total_ev[turn] = stat.win_ev[turn] + stat.deal_in_ev[turn] +
                                      stat.tenpai_ev[turn];
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    result.time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return result;
}
