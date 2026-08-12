#ifndef MAHJONG_CPP_EXPECTED_SCORE_CALCULATOR
#define MAHJONG_CPP_EXPECTED_SCORE_CALCULATOR

#include <array>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include "mahjong/types/types.hpp"

namespace mahjong
{
using MergedCount = std::array<int, 37>;
using SeparatedCount = std::array<int, 37>;

MergedCount create_wall(const TableConfig &table_config, const TableState &table_state,
                        const PlayerState &player, bool enable_reddora);

class ExpectedScoreCalculator
{
  public:
    struct Config
    {
        /* min turn to be calculated */
        int t_min = 1;
        /* max turn to be calculated */
        int t_max = 18;
        /* number of wall tiles */
        int sum = 0;
        /* search the range of possible (shanten number + extra) exchanges */
        int extra = 1;
        /* calculation shanten type */
        int shanten_type = ShantenFlag::All;
        /* enable red dora */
        bool enable_reddora = true;
        /* enable ura dora */
        bool enable_uradora = true;
        /* allow shanten down */
        bool enable_shanten_down = true;
        /* allow tegawari */
        bool enable_tegawari = true;
        /* disable shanten-down and tegawari automatically at 4+ shanten */
        bool auto_disable_deep_search = true;
        /* score closed tenpai continuations as riichi */
        bool enable_riichi = true;
        /* include one shanten-improving call; first-call ryanmen chi is excluded */
        bool enable_calls = false;
        /* include ippatsu, haitei, and houtei using turn-aware DP states */
        bool enable_turn_yaku = false;
        /* calculate value */
        bool calc_stats = true;
        /* Approximate share of completed wins scored as ron (0 restores legacy). */
        double ron_rate = 0.0;
        /* Live-wall tiles remaining after the current draw (-1 if unknown). */
        int remaining_tiles = -1;
        /* Stop paths when another player wins during a future turn. */
        bool enable_other_win_stop = false;
        /* Conditional other-player win hazards, indexed by turn 1..18. */
        std::array<double, 19> other_win_hazard = {
            0.0,    0.0002, 0.0008, 0.0029, 0.0078, 0.0170, 0.0305,
            0.0467, 0.0644, 0.0823, 0.0975, 0.1108, 0.1212, 0.1276,
            0.1312, 0.1323, 0.1309, 0.1170, 0.1170};
        /* Calculate per-yaku expected-score contributions. */
        bool calc_yaku_stats = false;
        /* Calculate exact Shapley allocation among scoring yaku and bonuses. */
        bool calc_shapley_stats = false;
        /* Yaku and bonus flags included in contribution statistics. */
        YakuFlags yaku_filter = Yaku::NormalMask | Yaku::YakumanMask | Yaku::NukiDora;
        /* Reserved policy discriminator for future call/meld search extensions. */
        std::uint8_t state_tag = 0;
    };

    struct YakuStat
    {
        YakuFlags yaku = Yaku::None;
        /* Probability that the selected expected-score policy wins with this yaku. */
        std::vector<double> occurrence_prob;
        /* Total expected score of wins containing this yaku. Overlaps are intentional. */
        std::vector<double> inclusive_score;
        /* Expected score lost when this yaku's value is removed. */
        std::vector<double> marginal_score;
        /* Exact Shapley allocation. Values sum to total expected score. */
        std::vector<double> shapley_score;
        /* Joint probability that a dynamic call occurred and this yaku wins. */
        std::vector<double> called_occurrence_prob;
        /* Joint Shapley contribution on dynamically called paths. */
        std::vector<double> called_shapley_score;
    };

    struct CallTileStat
    {
        int tile = Tile::Null;
        /* Unconditional probability that the selected policy calls this tile. */
        std::vector<double> probability;
    };

    struct Stat
    {
        /* tile */
        int tile;
        /* tenpai probability */
        std::vector<double> tenpai_prob;
        /* win probability */
        std::vector<double> win_prob;
        /* expected score */
        std::vector<double> exp_score;
        /* probability that the selected policy has called chi or pon */
        std::vector<double> call_prob;
        /* list of necessary tiles */
        std::vector<std::tuple<int, int>> necessary_tiles;
        /* shanten */
        int shanten;
        /* optional per-yaku expected-score contributions */
        std::vector<YakuStat> yaku_stats;
        /* probability of winning after a dynamic chi or pon */
        std::vector<double> call_win_prob;
        /* distribution of the single dynamic call by called tile */
        std::vector<CallTileStat> call_tile_stats;
    };

  private:
    static constexpr int MaxTurn = 18;

    struct CacheKey
    {
        CacheKey(const MergedCount &hand, std::uint8_t riichi_state,
                 std::uint8_t state_tag = 0);

        CacheKey with_riichi_state(std::uint8_t riichi_state) const noexcept;
        void change_tile(int tile, int delta) noexcept;

        bool operator==(const CacheKey &other) const
        {
            return lo == other.lo && hi == other.hi && melds == other.melds;
        }
        std::uint64_t lo = 0;
        std::uint64_t hi = 0;
        std::uint64_t melds = 0;
    };

    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey &key) const noexcept
        {
            std::uint64_t h = key.lo ^ (key.hi + 0x9e3779b97f4a7c15ULL + (key.lo << 6) +
                                        (key.lo >> 2));
            h ^= key.melds + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h ^= h >> 30;
            h *= 0xbf58476d1ce4e5b9ULL;
            h ^= h >> 27;
            h *= 0x94d049bb133111ebULL;
            h ^= h >> 31;
            return static_cast<std::size_t>(h);
        }
    };

    struct VertexData
    {
      public:
        VertexData() = default;
        explicit VertexData(const bool tenpai) : is_tenpai(tenpai)
        {
        }

        double tenpai_prob = 0.0;
        double win_prob = 0.0;
        double exp_score = 0.0;
        double call_prob = 0.0;
        double call_win_prob = 0.0;
        bool is_tenpai = false;
        bool has_open_meld = false;
        bool dynamic_called = false;
        std::int8_t dynamic_call_tile = -1;
    };

  public:
    struct ScoreData
    {
        double score = 0.0;
        std::array<double, Yaku::Length> occurrence{};
        std::array<double, Yaku::Length> inclusive{};
        std::array<double, Yaku::Length> marginal{};
        std::array<double, Yaku::Length> shapley{};
    };

  private:
    struct ContributionData
    {
        YakuFlags yaku = Yaku::None;
        double occurrence = 0.0;
        double inclusive = 0.0;
        double marginal = 0.0;
        double shapley = 0.0;
    };

    using Vertex = std::uint32_t;

    struct EdgeData
    {
        Vertex source;
        Vertex target;
        std::uint32_t next_out;
        std::uint32_t next_in;
        int weight;
        double score;
        std::uint32_t contribution_offset;
        std::uint16_t contribution_count;
        double last_score;
        std::uint32_t last_contribution_offset;
        std::uint16_t last_contribution_count;
    };

    struct Graph
    {
        static constexpr std::uint32_t NoEdge =
            std::numeric_limits<std::uint32_t>::max();

        Vertex add_vertex()
        {
            const Vertex vertex = static_cast<Vertex>(vertices.size());
            vertices.emplace_back();
            first_out_edges.push_back(NoEdge);
            first_in_edges.push_back(NoEdge);
            return vertex;
        }

        void add_edge(const Vertex source, const Vertex target, const int weight,
                      const ScoreData &score_data, const ScoreData &last_score_data)
        {
            const auto edge = static_cast<std::uint32_t>(edges.size());
            const auto append_contributions = [this](const ScoreData &data) {
                const auto offset = static_cast<std::uint32_t>(contributions.size());
                for (int i = 0; i < Yaku::Length; ++i) {
                    if (data.occurrence[i] != 0.0 || data.inclusive[i] != 0.0 ||
                        data.marginal[i] != 0.0 || data.shapley[i] != 0.0) {
                        contributions.push_back(ContributionData{
                            YakuFlags{1} << i, data.occurrence[i], data.inclusive[i],
                            data.marginal[i], data.shapley[i]});
                    }
                }
                return std::pair<std::uint32_t, std::uint16_t>{
                    offset, static_cast<std::uint16_t>(contributions.size() - offset)};
            };
            const auto [contribution_offset, contribution_count] =
                append_contributions(score_data);
            const auto [last_contribution_offset, last_contribution_count] =
                append_contributions(last_score_data);
            edges.push_back(EdgeData{source, target, first_out_edges[source],
                                     first_in_edges[target], weight, score_data.score,
                                     contribution_offset, contribution_count,
                                     last_score_data.score, last_contribution_offset,
                                     last_contribution_count});
            first_out_edges[source] = edge;
            first_in_edges[target] = edge;
        }

        bool has_edge(const Vertex source, const Vertex target) const
        {
            for (std::uint32_t edge = first_out_edges[source]; edge != NoEdge;
                 edge = edges[edge].next_out) {
                if (edges[edge].target == target) {
                    return true;
                }
            }
            return false;
        }

        std::size_t num_vertices() const
        {
            return vertices.size();
        }

        VertexData &operator[](const Vertex vertex)
        {
            return vertices[vertex];
        }

        const VertexData &operator[](const Vertex vertex) const
        {
            return vertices[vertex];
        }

        std::vector<VertexData> vertices;
        std::vector<EdgeData> edges;
        std::vector<std::uint32_t> first_out_edges;
        std::vector<std::uint32_t> first_in_edges;
        std::vector<ContributionData> contributions;
    };

    using Cache = boost::unordered_flat_map<CacheKey, Vertex, CacheKeyHash>;

    struct DrawEdge
    {
        std::uint32_t target;
        int weight;
        double score;
        std::uint32_t contribution_offset;
        std::uint16_t contribution_count;
        double last_score;
        std::uint32_t last_contribution_offset;
        std::uint16_t last_contribution_count;
    };

    struct SelectionEdge
    {
        std::uint32_t source;
    };

    struct CallOption
    {
        Vertex source;
        Vertex target;
        int tile;
        int weight;
        bool chi;
    };

    struct EdgeCsr
    {
        std::vector<DrawEdge> draw_edges;
        std::vector<SelectionEdge> selection_edges;
        std::vector<std::uint32_t> draw_edge_offsets;
        std::vector<std::uint32_t> selection_edge_offsets;
    };

  public:
    ExpectedScoreCalculator() = default;

    static std::tuple<std::vector<Stat>, int> calc(const Config &config,
                                                   const TableConfig &table_config,
                                                   const RoundState &round_state,
                                                   const TableState &table_state,
                                                   const PlayerState &player);

    static std::tuple<std::vector<Stat>, int>
    calc(const Config &config, const TableConfig &table_config,
         const RoundState &round_state, const TableState &table_state,
         const PlayerState &player, const MergedCount &wall);

  private:
    class GraphBuilder;

    static void calc_draw_hand(const Config &config, const PlayerState &player,
                               const TableConfig &table_config,
                               const RoundState &round_state,
                               const TableState &table_state, const MergedCount &wall,
                               const SeparatedCount &hand_counts,
                               GraphBuilder &graph_builder, std::vector<Stat> &stats);
    static void
    calc_discard_hand(const Config &config, PlayerState &player,
                      const TableConfig &table_config, const RoundState &round_state,
                      const TableState &table_state, const MergedCount &wall,
                      SeparatedCount &hand_counts, SeparatedCount &wall_counts,
                      GraphBuilder &graph_builder, std::vector<Stat> &stats);
    static EdgeCsr build_edge_csr(const Graph &graph);
    static void
    calc_stats(const Config &config, Graph &graph,
               const std::vector<Vertex> &draw_vertices,
               const std::vector<Vertex> &discard_vertices,
               const std::vector<std::pair<Vertex, Vertex>> &ippatsu_expiries,
               const std::vector<CallOption> &call_options, const EdgeCsr &edge_csr,
               const std::vector<Vertex> &root_vertices, std::vector<Stat> &stats);
    static std::tuple<std::vector<Stat>, int>
    calc_core(const Config &config, const TableConfig &table_config,
              const RoundState &round_state, const TableState &table_state,
              const PlayerState &player, const MergedCount &wall);
};
} // namespace mahjong

#endif /* MAHJONG_CPP_EXPECTED_SCORE_CALCULATOR */
