#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "mahjong/mahjong.hpp"

int main(int argc, char **argv)
{
    using namespace mahjong;

    const std::string hand = argc >= 2 ? argv[1] : "1234m23467p12406s";
    const int turn = argc >= 3 ? std::stoi(argv[2]) : 1;
    const double ron_rate = argc >= 4 ? std::stod(argv[3]) : 0.0;
    const bool enable_calls = argc >= 5 && std::stoi(argv[4]) != 0;
    if (turn < 1 || turn > 17) {
        std::cerr << "turn must be in [1, 17]\n";
        return 2;
    }

    TableConfig table_config;
    table_config.rule_flags = RuleFlag::Default;
    table_config.game_mode = GameMode::Yonma;

    RoundState round;
    round.round_wind = Tile::East;
    round.round_number = 1;

    TableState table;
    table.dora_indicators = {Tile::East};

    PlayerState player;
    player.hand = from_mpsz(hand);
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_min = 1;
    config.t_max = 18;
    config.extra = 1;
    config.ron_rate = ron_rate;
    config.enable_calls = enable_calls;
    config.enable_turn_yaku = true;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    auto [stats, searched] =
        ExpectedScoreCalculator::calc(config, table_config, round, table, player);
    std::sort(stats.begin(), stats.end(), [turn](const auto &lhs, const auto &rhs) {
        return lhs.exp_score[turn] > rhs.exp_score[turn];
    });

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "hand=" << hand << " turn=" << turn << " ron_rate=" << ron_rate
              << " calls=" << enable_calls << " searched=" << searched << '\n';
    for (const auto &stat : stats) {
        double shapley_sum = 0.0;
        for (const auto &entry : stat.yaku_stats) {
            shapley_sum += entry.shapley_score[turn];
        }
        std::cout << "discard=" << Tile::name(stat.tile) << " shanten=" << stat.shanten
                  << " win_prob=" << stat.win_prob[turn]
                  << " exp_score=" << stat.exp_score[turn]
                  << " call_prob=" << stat.call_prob[turn]
                  << " shapley_sum=" << shapley_sum
                  << " residual=" << stat.exp_score[turn] - shapley_sum << '\n';
        for (const auto &entry : stat.yaku_stats) {
            if (std::abs(entry.occurrence_prob[turn]) < 1e-12 &&
                std::abs(entry.shapley_score[turn]) < 1e-12) {
                continue;
            }
            std::cout << "  " << Yaku::name(entry.yaku)
                      << " occurrence=" << entry.occurrence_prob[turn]
                      << " shapley=" << entry.shapley_score[turn] << '\n';
        }
    }
}
