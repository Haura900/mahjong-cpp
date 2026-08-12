#include <iomanip>
#include <iostream>

#include "mahjong/mahjong.hpp"

int main()
{
    using namespace mahjong;

    TableConfig table_config;
    table_config.rule_flags = RuleFlag::Default;
    table_config.game_mode = GameMode::Yonma;

    RoundState round;
    round.round_wind = Tile::East;
    round.round_number = 1;

    TableState table;
    table.dora_indicators = {Tile::East};

    PlayerState player;
    player.hand = from_mpsz("23455m234p23467s");
    player.seat_wind = Tile::South;

    for (const double ron_rate : {0.0, 0.7}) {
        ExpectedScoreCalculator::Config config;
        config.t_max = 2;
        config.ron_rate = ron_rate;
        config.calc_yaku_stats = true;
        config.calc_shapley_stats = true;

        const auto [stats, searched] =
            ExpectedScoreCalculator::calc(config, table_config, round, table, player);
        std::cout << std::setprecision(17);
        std::cout << "ron_rate=" << ron_rate << " searched=" << searched
                  << " score=" << stats.front().exp_score[1] << '\n';
        for (const auto &yaku : stats.front().yaku_stats) {
            std::cout << Yaku::name(yaku.yaku) << '\t' << yaku.inclusive_score[1]
                      << '\t' << yaku.marginal_score[1] << '\t' << yaku.shapley_score[1]
                      << '\n';
        }
    }
}
