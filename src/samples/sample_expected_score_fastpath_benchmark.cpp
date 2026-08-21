#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "mahjong/mahjong.hpp"

namespace
{
using namespace mahjong;

struct Run
{
    std::vector<ExpectedScoreCalculator::Stat> stats;
    ExpectedScoreCalculator::Profile profile;
    int searched = 0;
    double elapsed_ms = 0.0;
};

Run run(const std::string &hand, const int t_max, const int repeat,
        const bool deep_search, const bool exp_score_only, const int deep_budget = 0,
        const bool high_shanten_only = false)
{
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
    config.t_max = t_max;
    config.extra = deep_search ? 1 : 0;
    config.auto_disable_deep_search = !deep_search;
    config.enable_calls = true;
    config.enable_turn_yaku = true;
    config.calc_exp_score_only = exp_score_only;
    config.track_deep_events = deep_budget != 0;
    config.max_deep_events_since_last_improvement =
        static_cast<std::uint8_t>(deep_budget);
    config.deep_event_limit_high_shanten_only = high_shanten_only;

    Run result;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repeat; ++i) {
        ExpectedScoreCalculator::Profile profile;
        auto [stats, searched] = ExpectedScoreCalculator::calc(
            config, table_config, round, table, player, &profile);
        result.stats = std::move(stats);
        result.searched = searched;
        result.profile += profile;
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start)
                            .count() /
                        repeat;
    result.profile.graph_build_us /= repeat;
    result.profile.csr_build_us /= repeat;
    result.profile.dp_us /= repeat;
    result.profile.draw_vertices /= repeat;
    result.profile.discard_vertices /= repeat;
    result.profile.edges /= repeat;
    result.profile.necessary_tile_calculator_calls /= repeat;
    result.profile.unnecessary_tile_calculator_calls /= repeat;
    result.profile.necessary_tile_calculator_us /= repeat;
    result.profile.unnecessary_tile_calculator_us /= repeat;
    for (auto &core : result.profile.core_invocations) {
        core.graph_build_us /= repeat;
        core.csr_build_us /= repeat;
        core.dp_us /= repeat;
        core.draw_vertices /= repeat;
        core.discard_vertices /= repeat;
        core.edges /= repeat;
    }
    result.profile.merge_turn_yaku_overlay_us /= repeat;
    return result;
}

const ExpectedScoreCalculator::Stat *find_stat(
    const std::vector<ExpectedScoreCalculator::Stat> &stats, const int tile)
{
    const auto found = std::find_if(stats.begin(), stats.end(),
                                    [tile](const auto &stat) {
                                        return stat.tile == tile;
                                    });
    return found == stats.end() ? nullptr : &*found;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 7) {
        std::cerr << "usage: sample_expected_score_fastpath_benchmark HAND [T_MAX] "
                     "[REPEAT] [DEEP_SEARCH] [DEEP_BUDGET] [HIGH_SHANTEN_ONLY]\n";
        return 2;
    }
    const std::string hand = argv[1];
    const int t_max = argc >= 3 ? std::atoi(argv[2]) : 4;
    const int repeat = argc >= 4 ? std::atoi(argv[3]) : 3;
    const bool deep_search = argc >= 5 && std::atoi(argv[4]) != 0;
    const int deep_budget = argc >= 6 ? std::atoi(argv[5]) : 0;
    const bool high_shanten_only = argc >= 7 && std::atoi(argv[6]) != 0;
    if (t_max < 1 || t_max > 18 || repeat < 1) {
        std::cerr << "T_MAX must be 1..18 and REPEAT must be positive\n";
        return 2;
    }

    const Run full = run(hand, t_max, repeat, deep_search, false, deep_budget,
                         high_shanten_only);
    const Run fast = run(hand, t_max, repeat, deep_search, true, deep_budget,
                         high_shanten_only);
    double max_abs_diff = 0.0;
    bool recommendations_match = true;
    int full_best = Tile::Null;
    int fast_best = Tile::Null;
    double full_best_score = -1.0;
    double fast_best_score = -1.0;
    for (const auto &stat : full.stats) {
        const auto *fast_stat = find_stat(fast.stats, stat.tile);
        if (fast_stat == nullptr || fast_stat->exp_score.size() != stat.exp_score.size()) {
            std::cerr << "fast-path result shape mismatch\n";
            return 1;
        }
        for (std::size_t turn = 0; turn < stat.exp_score.size(); ++turn) {
            max_abs_diff = std::max(
                max_abs_diff, std::abs(stat.exp_score[turn] - fast_stat->exp_score[turn]));
        }
        const double full_value = stat.exp_score[t_max];
        const double fast_value = fast_stat->exp_score[t_max];
        if (full_value > full_best_score) {
            full_best_score = full_value;
            full_best = stat.tile;
        }
        if (fast_value > fast_best_score) {
            fast_best_score = fast_value;
            fast_best = stat.tile;
        }
    }
    recommendations_match = full_best == fast_best;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "hand=" << hand << " t_max=" << t_max << " repeat=" << repeat
              << " deep_search=" << deep_search
              << " extra=" << (deep_search ? 1 : 0)
              << " deep_budget=" << deep_budget
              << " high_shanten_only=" << high_shanten_only
              << "\nfull_ms=" << full.elapsed_ms << " fast_ms=" << fast.elapsed_ms
              << " speedup=" << full.elapsed_ms / fast.elapsed_ms << "x\n"
              << "full_graph_ms=" << full.profile.graph_build_us / 1000.0
              << " fast_graph_ms=" << fast.profile.graph_build_us / 1000.0
              << " full_dp_ms=" << full.profile.dp_us / 1000.0
              << " fast_dp_ms=" << fast.profile.dp_us / 1000.0 << "\n"
              << "full_states=" << full.searched << " fast_states=" << fast.searched
              << " full_edges=" << full.profile.edges
              << " fast_edges=" << fast.profile.edges << "\n"
              << "max_abs_exp_score_diff=" << max_abs_diff
              << " full_best=" << full_best << " fast_best=" << fast_best
              << " recommendation_match=" << recommendations_match << "\n";
    std::cout << "full_necessary_calls=" << full.profile.necessary_tile_calculator_calls
              << " full_necessary_ms="
              << full.profile.necessary_tile_calculator_us / 1000.0
              << " full_unnecessary_calls="
              << full.profile.unnecessary_tile_calculator_calls
              << " full_unnecessary_ms="
              << full.profile.unnecessary_tile_calculator_us / 1000.0
              << " fast_necessary_calls=" << fast.profile.necessary_tile_calculator_calls
              << " fast_necessary_ms="
              << fast.profile.necessary_tile_calculator_us / 1000.0
              << " fast_unnecessary_calls=" << fast.profile.unnecessary_tile_calculator_calls
              << " fast_unnecessary_ms="
              << fast.profile.unnecessary_tile_calculator_us / 1000.0 << "\n";
    std::cout << "full_deep_event_prunes=" << full.profile.deep_event_prunes
              << " fast_deep_event_prunes=" << fast.profile.deep_event_prunes << "\n";
    const auto print_distribution = [](const char *name, const Run &result) {
        for (std::size_t shanten = 0;
             shanten < result.profile.deep_event_draw_states.size(); ++shanten) {
            std::cout << name << "_deep_states_shanten="
                      << (shanten == 4 ? "4+" : std::to_string(shanten));
            for (const auto count : result.profile.deep_event_draw_states[shanten]) {
                std::cout << ' ' << count;
            }
            std::cout << "\n";
        }
    };
    if (deep_budget != 0) {
        print_distribution("full", full);
        print_distribution("fast", fast);
    }
    const auto print_cores = [](const char *name, const Run &result) {
        static constexpr std::array<const char *, 3> labels = {
            "base", "turn_on", "turn_off"};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const auto &core = result.profile.core_invocations[i];
            std::cout << name << "_" << labels[i]
                      << " graph_ms=" << core.graph_build_us / 1000.0
                      << " csr_ms=" << core.csr_build_us / 1000.0
                      << " dp_ms=" << core.dp_us / 1000.0
                      << " draw_vertices=" << core.draw_vertices
                      << " discard_vertices=" << core.discard_vertices
                      << " edges=" << core.edges << "\n";
        }
        std::cout << name << "_merge_ms="
                  << result.profile.merge_turn_yaku_overlay_us / 1000.0 << "\n";
    };
    print_cores("full", full);
    print_cores("fast", fast);
    return max_abs_diff <= 1e-5 && recommendations_match ? 0 : 1;
}
