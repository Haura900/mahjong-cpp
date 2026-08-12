#define CATCH_CONFIG_MAIN
#include <algorithm>
#include <cmath>

#include <catch2/catch.hpp>

#include "mahjong/mahjong.hpp"

using namespace mahjong;

namespace
{

struct Context
{
    TableConfig table_config;
    RoundState round;
    TableState table;

    Context()
    {
        table_config.rule_flags = RuleFlag::Default;
        table_config.game_mode = GameMode::Yonma;
        round.round_wind = Tile::East;
        round.round_number = 1;
        table.dora_indicators = {Tile::East};
    }
};

const ExpectedScoreCalculator::Stat &
stat_for(const std::vector<ExpectedScoreCalculator::Stat> &stats, const int tile)
{
    const auto found =
        std::find_if(stats.begin(), stats.end(),
                     [tile](const auto &stat) { return stat.tile == tile; });
    REQUIRE(found != stats.end());
    return *found;
}

const ExpectedScoreCalculator::YakuStat &
yaku_for(const ExpectedScoreCalculator::Stat &stat, const YakuFlags yaku)
{
    const auto found =
        std::find_if(stat.yaku_stats.begin(), stat.yaku_stats.end(),
                     [yaku](const auto &entry) { return entry.yaku == yaku; });
    REQUIRE(found != stat.yaku_stats.end());
    return *found;
}

PlayerState player_for(const char *hand)
{
    PlayerState player;
    player.hand = from_mpsz(hand);
    player.seat_wind = Tile::East;
    return player;
}

double shapley_sum(const ExpectedScoreCalculator::Stat &stat, const int turn)
{
    double sum = 0.0;
    for (const auto &entry : stat.yaku_stats) {
        sum += entry.shapley_score[turn];
    }
    return sum;
}

} // namespace

TEST_CASE("ron_rate zero reproduces upstream regression fixtures")
{
    Context context;
    ExpectedScoreCalculator::Config config;
    config.t_max = 8;
    config.ron_rate = 0.0;

    SECTION("closed tenpai discard choices")
    {
        const auto [stats, searched] = ExpectedScoreCalculator::calc(
            config, context.table_config, context.round, context.table,
            player_for("222567m345p33667s"));
        REQUIRE(searched == 173);
        REQUIRE(stats.size() == 10);
        const auto &best = stat_for(stats, Tile::Souzu6);
        CHECK(best.shanten == 0);
        CHECK(best.tenpai_prob[1] == 1.0);
        CHECK(best.win_prob[1] == Approx(0.39100065493145386).epsilon(1e-13));
        CHECK(best.exp_score[1] == Approx(3335.6637484395037).epsilon(1e-13));
    }

    SECTION("seven pairs draw state")
    {
        const auto [stats, searched] =
            ExpectedScoreCalculator::calc(config, context.table_config, context.round,
                                          context.table, player_for("1122334455667m"));
        REQUIRE(searched == 402);
        REQUIRE(stats.size() == 1);
        CHECK(stats[0].tile == Tile::Null);
        CHECK(stats[0].win_prob[1] == Approx(0.37021420184868375).epsilon(1e-13));
        CHECK(stats[0].exp_score[1] == Approx(10862.237033137948).epsilon(1e-13));
    }

    SECTION("existing open meld representation")
    {
        PlayerState player = player_for("234m456p789s55z");
        player.melds = {{MeldType::Pon,
                         {Tile::East, Tile::East, Tile::East},
                         Tile::East,
                         SeatType::Kamicha}};
        const auto [stats, searched] = ExpectedScoreCalculator::calc(
            config, context.table_config, context.round, context.table, player);
        REQUIRE(searched == 18);
        const auto &east = stat_for(stats, Tile::Manzu2);
        CHECK(east.win_prob[1] == Approx(0.39100065493145386).epsilon(1e-13));
        CHECK(east.exp_score[1] == Approx(1319.627210393657).epsilon(1e-13));
    }
}

TEST_CASE("four-shanten state is calculated")
{
    Context context;
    PlayerState player = player_for("147m258p369s11122z");
    ExpectedScoreCalculator::Config config;
    config.t_max = 4;
    config.extra = 0;

    const int shanten = std::get<1>(
        ShantenCalculator::calc(player.hand, player.num_melds(), config.shanten_type,
                                context.table_config.game_mode));
    REQUIRE(shanten == 4);

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    CHECK(searched == 15833);
    REQUIRE(stats.size() == 10);
    for (const auto &stat : stats) {
        REQUIRE(stat.tenpai_prob.size() == 5);
        REQUIRE(stat.win_prob.size() == 5);
        REQUIRE(stat.exp_score.size() == 5);
        CHECK(std::isfinite(stat.exp_score[1]));
    }
}

TEST_CASE("exact DP reports inclusive and marginal yaku contributions")
{
    Context context;
    PlayerState player = player_for("23455m234p23467s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config tsumo_config;
    tsumo_config.t_max = 2;
    tsumo_config.calc_yaku_stats = true;
    tsumo_config.calc_shapley_stats = true;
    const auto [tsumo_stats, tsumo_searched] = ExpectedScoreCalculator::calc(
        tsumo_config, context.table_config, context.round, context.table, player);
    REQUIRE(tsumo_searched == 170);
    REQUIRE(tsumo_stats.size() == 1);
    const auto &tsumo = tsumo_stats.front();
    CHECK(tsumo.exp_score[1] == Approx(561.98347107438019).epsilon(1e-13));
    CHECK(shapley_sum(tsumo, 1) == Approx(tsumo.exp_score[1]).margin(1e-10));

    const auto &sanshoku = yaku_for(tsumo, Yaku::MixedTripleSequence);
    CHECK(sanshoku.occurrence_prob[1] == Approx(tsumo.win_prob[1]).epsilon(1e-13));
    CHECK(sanshoku.inclusive_score[1] == Approx(561.98347107438019).epsilon(1e-13));
    CHECK(sanshoku.marginal_score[1] == Approx(362.80991735537191).epsilon(1e-13));
    CHECK(sanshoku.inclusive_score[1] > sanshoku.marginal_score[1]);
    CHECK(sanshoku.shapley_score[1] == Approx(223.96694214876032).epsilon(1e-13));

    const auto &pinfu = yaku_for(tsumo, Yaku::Pinfu);
    CHECK(pinfu.marginal_score[1] == Approx(195.04132231404958).epsilon(1e-13));

    ExpectedScoreCalculator::Config mixed_config = tsumo_config;
    mixed_config.ron_rate = 0.7;
    const auto [mixed_stats, mixed_searched] = ExpectedScoreCalculator::calc(
        mixed_config, context.table_config, context.round, context.table, player);
    REQUIRE(mixed_searched == tsumo_searched);
    const auto &mixed = mixed_stats.front();
    const double mixed_denominator = 0.3 + 0.7 * tsumo.win_prob[1];
    CHECK(mixed.win_prob[1] ==
          Approx(tsumo.win_prob[1] / mixed_denominator).epsilon(1e-13));
    CHECK(mixed.exp_score[1] ==
          Approx(526.69421487603302 / mixed_denominator).epsilon(1e-13));
    CHECK(shapley_sum(mixed, 1) == Approx(mixed.exp_score[1]).margin(1e-10));
    CHECK(yaku_for(mixed, Yaku::Tsumo).inclusive_score[1] ==
          Approx(yaku_for(tsumo, Yaku::Tsumo).inclusive_score[1] * 0.3 /
                 mixed_denominator)
              .epsilon(1e-13));
    CHECK(yaku_for(mixed, Yaku::Tsumo).occurrence_prob[1] ==
          Approx(yaku_for(tsumo, Yaku::Tsumo).occurrence_prob[1] * 0.3 /
                 mixed_denominator)
              .epsilon(1e-13));
    CHECK(yaku_for(mixed, Yaku::Tsumo).occurrence_prob[1] ==
          Approx(mixed.win_prob[1] * 0.3).epsilon(1e-13));
}

TEST_CASE("other-player win hazard terminates self-win paths")
{
    Context context;
    PlayerState player = player_for("23455m234p23467s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config baseline_config;
    baseline_config.t_max = 2;
    baseline_config.calc_yaku_stats = true;
    baseline_config.calc_shapley_stats = true;
    baseline_config.other_win_hazard.fill(0.0);
    baseline_config.other_win_hazard[2] = 0.25;

    const auto [baseline_stats, baseline_searched] = ExpectedScoreCalculator::calc(
        baseline_config, context.table_config, context.round, context.table, player);
    REQUIRE(baseline_searched > 0);
    const auto &baseline = baseline_stats.front();

    auto stopped_config = baseline_config;
    stopped_config.enable_other_win_stop = true;
    const auto [stopped_stats, stopped_searched] = ExpectedScoreCalculator::calc(
        stopped_config, context.table_config, context.round, context.table, player);
    REQUIRE(stopped_searched == baseline_searched);
    const auto &stopped = stopped_stats.front();

    CHECK(stopped.tenpai_prob[1] == Approx(baseline.tenpai_prob[1]).margin(1e-13));
    CHECK(stopped.win_prob[1] == Approx(baseline.win_prob[1] * 0.75).margin(1e-13));
    CHECK(stopped.exp_score[1] == Approx(baseline.exp_score[1] * 0.75).margin(1e-10));
    CHECK(yaku_for(stopped, Yaku::MixedTripleSequence).occurrence_prob[1] ==
          Approx(yaku_for(baseline, Yaku::MixedTripleSequence).occurrence_prob[1] *
                 0.75)
              .margin(1e-13));
    CHECK(shapley_sum(stopped, 1) == Approx(stopped.exp_score[1]).margin(1e-10));
}

TEST_CASE("yakuhai pon transitions are optional and report call probability")
{
    Context context;
    PlayerState player = player_for("123m456p789s2455z");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 4;
    disabled.enable_shanten_down = false;
    disabled.enable_tegawari = false;
    const auto [closed_stats, closed_searched] = ExpectedScoreCalculator::calc(
        disabled, context.table_config, context.round, context.table, player);
    REQUIRE(closed_searched > 0);
    REQUIRE(closed_stats.size() == 1);
    CHECK(closed_stats.front().call_prob[1] == 0.0);

    auto enabled = disabled;
    enabled.enable_calls = true;
    const auto [call_stats, call_searched] = ExpectedScoreCalculator::calc(
        enabled, context.table_config, context.round, context.table, player);
    REQUIRE(call_searched > closed_searched);
    REQUIRE(call_stats.size() == 1);
    CHECK(call_stats.front().call_prob[1] > 0.0);
    CHECK(call_stats.front().call_prob[1] <= 1.0);
    CHECK(call_stats.front().exp_score[1] >= closed_stats.front().exp_score[1]);
}

TEST_CASE("non-yakuhai calls are allowed when they improve shanten")
{
    Context context;
    PlayerState player = player_for("11234m456p789s44z");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 4;
    disabled.enable_shanten_down = false;
    disabled.enable_tegawari = false;
    const auto [closed_stats, closed_searched] = ExpectedScoreCalculator::calc(
        disabled, context.table_config, context.round, context.table, player);

    auto enabled = disabled;
    enabled.enable_calls = true;
    const auto [call_stats, call_searched] = ExpectedScoreCalculator::calc(
        enabled, context.table_config, context.round, context.table, player);
    REQUIRE(call_stats.size() == closed_stats.size());
    CHECK(call_searched > closed_searched);
    CHECK(call_stats.front().call_prob[1] >= 0.0);
    CHECK(call_stats.front().exp_score[1] >= closed_stats.front().exp_score[1]);
}

TEST_CASE("call model skips hands without a shanten-improving chi or pon")
{
    Context context;
    PlayerState player = player_for("147m258p369s1234z");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 4;
    disabled.enable_shanten_down = false;
    disabled.enable_tegawari = false;
    const auto [closed_stats, closed_searched] = ExpectedScoreCalculator::calc(
        disabled, context.table_config, context.round, context.table, player);

    auto enabled = disabled;
    enabled.enable_calls = true;
    const auto [call_stats, call_searched] = ExpectedScoreCalculator::calc(
        enabled, context.table_config, context.round, context.table, player);
    REQUIRE(call_stats.size() == closed_stats.size());
    CHECK(call_searched == closed_searched);
    CHECK(call_stats.front().call_prob[1] == 0.0);
}

TEST_CASE("shanten-improving chi from kamicha is added to the graph")
{
    Context context;
    PlayerState player = player_for("12m4569p789s1234z");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 2;
    disabled.enable_shanten_down = false;
    disabled.enable_tegawari = false;
    const auto [closed_stats, closed_searched] = ExpectedScoreCalculator::calc(
        disabled, context.table_config, context.round, context.table, player);

    auto enabled = disabled;
    enabled.enable_calls = true;
    const auto [call_stats, call_searched] = ExpectedScoreCalculator::calc(
        enabled, context.table_config, context.round, context.table, player);
    REQUIRE(call_stats.size() == closed_stats.size());
    CHECK(call_searched > closed_searched);
}

TEST_CASE("first call excludes ryanmen chi but keeps penchan chi")
{
    Context context;
    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 2;
    disabled.enable_shanten_down = false;
    disabled.enable_tegawari = false;
    auto enabled = disabled;
    enabled.enable_calls = true;

    PlayerState ryanmen = player_for("23m45p111999s147z");
    ryanmen.seat_wind = Tile::South;
    const auto [ryanmen_closed, ryanmen_closed_searched] =
        ExpectedScoreCalculator::calc(disabled, context.table_config,
                                      context.round, context.table, ryanmen);
    const auto [ryanmen_calls, ryanmen_call_searched] =
        ExpectedScoreCalculator::calc(enabled, context.table_config,
                                      context.round, context.table, ryanmen);
    CHECK(ryanmen_call_searched == ryanmen_closed_searched);

    PlayerState penchan = player_for("12m45p111999s147z");
    penchan.seat_wind = Tile::South;
    const auto [penchan_closed, penchan_closed_searched] =
        ExpectedScoreCalculator::calc(disabled, context.table_config,
                                      context.round, context.table, penchan);
    const auto [penchan_calls, penchan_call_searched] =
        ExpectedScoreCalculator::calc(enabled, context.table_config,
                                      context.round, context.table, penchan);
    CHECK(penchan_call_searched > penchan_closed_searched);
}

TEST_CASE("dynamic call branches do not re-expand tegawari or shanten-down")
{
    Context context;
    PlayerState player = player_for("237m13668p678s667z");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.enable_calls = true;
    config.enable_shanten_down = true;
    config.enable_tegawari = true;
    config.extra = 1;
    config.t_max = 4;
    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);

    REQUIRE_FALSE(stats.empty());
    CHECK(searched < 100000);
}

TEST_CASE("exact Shapley allocation is efficient for every discard")
{
    Context context;
    PlayerState player = player_for("1234m23467p12406s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_max = 4;
    config.extra = 1;
    config.calc_shapley_stats = true;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(searched == 5557);
    REQUIRE(stats.size() == 14);
    for (const auto &stat : stats) {
        for (int turn = 1; turn < config.t_max; ++turn) {
            CHECK(shapley_sum(stat, turn) == Approx(stat.exp_score[turn]).margin(1e-9));
        }
    }
}

TEST_CASE("turn-aware DP includes ippatsu and count-selected haitei")
{
    Context context;
    PlayerState player = player_for("1239m12377p12345s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_max = 18;
    config.enable_shanten_down = false;
    config.enable_tegawari = false;
    config.enable_turn_yaku = true;
    config.ron_rate = 0.7;
    config.remaining_tiles = 4;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(searched > 0);
    const auto &discard = stat_for(stats, Tile::Manzu9);

    CHECK(yaku_for(discard, Yaku::Ippatsu).inclusive_score[1] > 0.0);
    CHECK(yaku_for(discard, Yaku::UnderTheSea).inclusive_score[1] > 0.0);
    CHECK(std::none_of(discard.yaku_stats.begin(), discard.yaku_stats.end(),
                       [](const auto &entry) {
                           return entry.yaku == Yaku::UnderTheRiver;
                       }));
    CHECK(shapley_sum(discard, 1) == Approx(discard.exp_score[1]).margin(1e-9));
}

TEST_CASE("tsumo-only setting suppresses houtei on the final tile")
{
    Context context;
    PlayerState player = player_for("1239m12377p12345s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_max = 18;
    config.enable_shanten_down = false;
    config.enable_tegawari = false;
    config.enable_turn_yaku = true;
    config.ron_rate = 0.0;
    config.remaining_tiles = 3;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(searched > 0);
    const auto &discard = stat_for(stats, Tile::Manzu9);
    const auto houtei =
        std::find_if(discard.yaku_stats.begin(), discard.yaku_stats.end(),
                     [](const auto &entry) {
                         return entry.yaku == Yaku::UnderTheRiver;
                     });

    const auto haitei =
        std::find_if(discard.yaku_stats.begin(), discard.yaku_stats.end(),
                     [](const auto &entry) {
                         return entry.yaku == Yaku::UnderTheSea;
                     });
    CHECK(haitei == discard.yaku_stats.end());
    CHECK(houtei == discard.yaku_stats.end());
    CHECK(shapley_sum(discard, 1) == Approx(discard.exp_score[1]).margin(1e-9));
}

TEST_CASE("remaining live-wall count makes haitei and houtei exclusive")
{
    Context context;
    PlayerState player = player_for("1239m12377p12345s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config config;
    config.t_max = 18;
    config.enable_shanten_down = false;
    config.enable_tegawari = false;
    config.enable_turn_yaku = true;
    config.ron_rate = 0.7;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    const auto find_yaku = [](const auto &stat, const YakuFlags yaku) {
        return std::find_if(stat.yaku_stats.begin(), stat.yaku_stats.end(),
                            [yaku](const auto &entry) {
                                return entry.yaku == yaku;
                            });
    };

    config.remaining_tiles = 4;
    const auto [haitei_stats, haitei_searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(haitei_searched > 0);
    const auto &haitei_discard = stat_for(haitei_stats, Tile::Manzu9);
    CHECK(yaku_for(haitei_discard, Yaku::UnderTheSea).occurrence_prob[1] > 0.0);
    CHECK(find_yaku(haitei_discard, Yaku::UnderTheRiver) ==
          haitei_discard.yaku_stats.end());

    config.remaining_tiles = 3;
    const auto [houtei_stats, houtei_searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(houtei_searched > 0);
    const auto &houtei_discard = stat_for(houtei_stats, Tile::Manzu9);
    CHECK(find_yaku(houtei_discard, Yaku::UnderTheSea) ==
          houtei_discard.yaku_stats.end());
    CHECK(yaku_for(houtei_discard, Yaku::UnderTheRiver).occurrence_prob[1] > 0.0);
}

TEST_CASE("ippatsu expires after one missed draw and riichi hand stays fixed")
{
    Context context;
    PlayerState player = player_for("4568m23355p55678s");

    ExpectedScoreCalculator::Config config;
    config.t_max = 18;
    config.enable_shanten_down = true;
    config.enable_tegawari = true;
    config.enable_riichi = true;
    config.enable_turn_yaku = true;
    config.ron_rate = 0.7;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(searched > 0);
    const auto &discard = stat_for(stats, Tile::Manzu8);
    const auto &riichi = yaku_for(discard, Yaku::Riichi);
    const auto &ippatsu = yaku_for(discard, Yaku::Ippatsu);

    CHECK(ippatsu.inclusive_score[1] > 0.0);
    CHECK(riichi.inclusive_score[1] > ippatsu.inclusive_score[1]);
    CHECK(riichi.shapley_score[1] > ippatsu.shapley_score[1]);
    CHECK(shapley_sum(discard, 1) == Approx(discard.exp_score[1]).margin(1e-9));
}

TEST_CASE("turn-yaku overlay prevents tegawari path merging from inflating ippatsu")
{
    Context context;
    PlayerState player = player_for("0455m345p2468899s");

    ExpectedScoreCalculator::Config config;
    config.t_max = 18;
    config.extra = 1;
    config.enable_tegawari = true;
    config.enable_turn_yaku = true;
    config.ron_rate = 0.7;
    config.calc_yaku_stats = true;
    config.calc_shapley_stats = true;

    const auto [stats, searched] = ExpectedScoreCalculator::calc(
        config, context.table_config, context.round, context.table, player);
    REQUIRE(searched > 0);
    const auto &discard = stat_for(stats, Tile::Manzu4);
    const auto &ippatsu = yaku_for(discard, Yaku::Ippatsu);

    CHECK(ippatsu.occurrence_prob[10] < discard.win_prob[10] * 0.4);
    CHECK(shapley_sum(discard, 10) == Approx(discard.exp_score[10]).margin(1e-8));
}

TEST_CASE("riichi setting applies to future search states")
{
    Context context;
    PlayerState player = player_for("1234m23467p12406s");
    player.seat_wind = Tile::South;

    ExpectedScoreCalculator::Config disabled;
    disabled.t_max = 4;
    disabled.enable_riichi = false;
    disabled.calc_yaku_stats = true;
    const auto [disabled_stats, disabled_searched] = ExpectedScoreCalculator::calc(
        disabled, context.table_config, context.round, context.table, player);
    REQUIRE(disabled_searched > 0);
    for (const auto &stat : disabled_stats) {
        CHECK(
            std::none_of(stat.yaku_stats.begin(), stat.yaku_stats.end(),
                         [](const auto &entry) { return entry.yaku == Yaku::Riichi; }));
    }

    ExpectedScoreCalculator::Config enabled = disabled;
    enabled.enable_riichi = true;
    const auto [enabled_stats, enabled_searched] = ExpectedScoreCalculator::calc(
        enabled, context.table_config, context.round, context.table, player);
    REQUIRE(enabled_searched > 0);
    CHECK(std::any_of(enabled_stats.begin(), enabled_stats.end(), [](const auto &stat) {
        return std::any_of(
            stat.yaku_stats.begin(), stat.yaku_stats.end(),
            [](const auto &entry) { return entry.yaku == Yaku::Riichi; });
    }));
}

TEST_CASE("invalid ron_rate is rejected")
{
    Context context;
    auto player = player_for("1122334455667m");
    ExpectedScoreCalculator::Config config;
    config.ron_rate = 1.01;
    CHECK_THROWS_AS(ExpectedScoreCalculator::calc(config, context.table_config,
                                                  context.round, context.table, player),
                    std::invalid_argument);
    config.ron_rate = 0.0;
    config.remaining_tiles = 71;
    CHECK_THROWS_AS(ExpectedScoreCalculator::calc(config, context.table_config,
                                                  context.round, context.table, player),
                    std::invalid_argument);
}
