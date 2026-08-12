#include <chrono>
#include <iostream>

#include "mahjong/mahjong.hpp"

int main(int argc, char **argv)
{
    using namespace mahjong;
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: sample_expected_score_probe HAND [EXTRA]\n";
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
    player.hand = from_mpsz(argv[1]);
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_max = 4;
    config.extra = argc == 3 ? std::stoi(argv[2]) : 0;
    const int shanten = std::get<1>(ShantenCalculator::calc(
        player.hand, player.num_melds(), config.shanten_type, table_config.game_mode));
    const auto start = std::chrono::steady_clock::now();
    const auto [stats, searched] =
        ExpectedScoreCalculator::calc(config, table_config, round, table, player);
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "hand=" << argv[1] << " shanten=" << shanten << " states=" << searched
              << " stats=" << stats.size() << " elapsed_ms=" << elapsed << '\n';
}
