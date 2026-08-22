#include "request_processor.hpp"

#include <chrono>
#include <numeric>
#include <stdexcept>

using namespace mahjong;

CalculationResult calculate_result(const Request &req)
{
    CalculationResult result;

    result.config = req.config;
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
    // Adaptive policies own high-shanten pruning and must be allowed to return
    // to full search after the state improves to two shanten or less.
    if (result.config.auto_disable_deep_search &&
        result.config.adaptive_deep_search_mode == 0 && result.shanten >= 4) {
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
                                      req.table_state, req.player, req.wall,
                                      &result.profile);
    const auto end = std::chrono::steady_clock::now();
    result.time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return result;
}
