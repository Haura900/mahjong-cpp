#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "mahjong/mahjong.hpp"

namespace
{

struct Case
{
    const char *name;
    const char *hand;
    int t_max;
    int extra;
    std::vector<mahjong::Meld> melds;
};

} // namespace

int main()
{
    using namespace mahjong;

    const std::vector<Case> cases = {
        {"closed-tenpai", "222567m345p33667s", 8, 1, {}},
        {"seven-pairs-tenpai", "1122334455667m", 8, 1, {}},
        {"open-hand",
         "234m456p789s55z",
         8,
         1,
         {{MeldType::Pon,
           {Tile::East, Tile::East, Tile::East},
           Tile::East,
           SeatType::Kamicha}}},
        {"closed-progress", "147m258p369s12345z", 8, 0, {}},
    };

    TableConfig table_config;
    table_config.rule_flags = RuleFlag::Default;
    table_config.game_mode = GameMode::Yonma;

    RoundState round;
    round.round_wind = Tile::East;
    round.round_number = 1;

    TableState table;
    table.dora_indicators = {Tile::East};

    std::cout << std::setprecision(17);
    for (const auto &test_case : cases) {
        PlayerState player;
        player.hand = from_mpsz(test_case.hand);
        player.melds = test_case.melds;
        player.seat_wind = Tile::East;

        ExpectedScoreCalculator::Config config;
        config.t_max = test_case.t_max;
        config.extra = test_case.extra;

        const auto start = std::chrono::steady_clock::now();
        const auto [stats, searched] =
            ExpectedScoreCalculator::calc(config, table_config, round, table, player);
        const auto elapsed = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();

        std::cout << "CASE\t" << test_case.name << '\t' << test_case.hand << '\t'
                  << searched << '\t' << elapsed << '\n';
        for (const auto &stat : stats) {
            std::cout << "STAT\t" << test_case.name << '\t' << stat.tile << '\t'
                      << stat.shanten;
            for (int t = 1; t <= config.t_max; ++t) {
                std::cout << '\t' << stat.tenpai_prob[t] << '\t' << stat.win_prob[t]
                          << '\t' << stat.exp_score[t];
            }
            std::cout << '\n';
        }
    }
}
