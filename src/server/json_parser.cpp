#include "json_parser.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <spdlog/spdlog.h>

#include "mahjong/mahjong.hpp"
#include "mahjong/core/runtime_data_path.hpp"

using namespace mahjong;

namespace
{

const rapidjson::SchemaDocument &get_request_schema()
{
    static const rapidjson::SchemaDocument schema = []() {
        const std::filesystem::path schema_path =
            mahjong::detail::runtime_data_directory() / "request_schema.json";

        std::ifstream ifs(schema_path.string());
        if (!ifs.is_open()) {
            throw std::runtime_error(fmt::format("Failed to open JSON schema: path={}.",
                                                 schema_path.string()));
        }

        rapidjson::Document schema_doc;
        rapidjson::IStreamWrapper isw(ifs);
        if (schema_doc.ParseStream(isw).HasParseError()) {
            throw std::runtime_error(fmt::format(
                "Failed to parse JSON schema: path={}.", schema_path.string()));
        }

        return rapidjson::SchemaDocument(schema_doc);
    }();

    return schema;
}

// Build the internal request representation from a parsed JSON value.
Request make_request(const rapidjson::Value &doc)
{
    Request req;
    req.table_config.game_mode = doc["game_mode"].GetInt();
    req.round_state.round_wind = doc["round_wind"].GetInt();
    req.player.seat_wind = doc["seat_wind"].GetInt();

    const auto dora_indicators = doc["dora_indicators"].GetArray();
    req.table_state.dora_indicators.reserve(dora_indicators.Size());
    for (const auto &x : dora_indicators) {
        req.table_state.dora_indicators.push_back(x.GetInt());
    }

    std::vector<int> hand;
    const auto hand_array = doc["hand"].GetArray();
    hand.reserve(hand_array.Size());
    for (const auto &x : hand_array) {
        hand.push_back(x.GetInt());
    }
    req.player.hand = from_array(hand);

    const auto melds = doc["melds"].GetArray();
    req.player.melds.reserve(melds.Size());
    for (const auto &meld : melds) {
        int meld_type = meld["type"].GetInt();
        std::vector<int> meld_tiles;
        const auto meld_tile_array = meld["tiles"].GetArray();
        meld_tiles.reserve(meld_tile_array.Size());
        for (const auto &x : meld_tile_array) {
            meld_tiles.push_back(x.GetInt());
        }
        req.player.melds.push_back(Meld{meld_type, meld_tiles});
    }

    if (doc.HasMember("nuki_count")) {
        req.player.nuki_count = doc["nuki_count"].GetInt();
    }

    req.config.enable_reddora = doc["enable_reddora"].GetBool();
    req.config.enable_shanten_down = doc["enable_shanten_down"].GetBool();
    req.config.enable_tegawari = doc["enable_tegawari"].GetBool();
    if (doc.HasMember("auto_disable_deep_search")) {
        req.config.auto_disable_deep_search = doc["auto_disable_deep_search"].GetBool();
    }
    if (doc.HasMember("enable_riichi")) {
        req.config.enable_riichi = doc["enable_riichi"].GetBool();
    }
    if (doc.HasMember("enable_calls")) {
        req.config.enable_calls = doc["enable_calls"].GetBool();
    }
    if (doc.HasMember("enable_turn_yaku")) {
        req.config.enable_turn_yaku = doc["enable_turn_yaku"].GetBool();
    }
    req.config.enable_uradora = doc["enable_uradora"].GetBool();
    if (doc.HasMember("t_min")) {
        req.config.t_min = doc["t_min"].GetInt();
    }
    if (doc.HasMember("t_max")) {
        req.config.t_max = doc["t_max"].GetInt();
    }
    if (doc.HasMember("extra")) {
        req.config.extra = doc["extra"].GetInt();
    }
    if (doc.HasMember("calc_stats")) {
        req.config.calc_stats = doc["calc_stats"].GetBool();
        req.calc_stats_explicit = true;
    }
    if (doc.HasMember("calc_exp_score_only")) {
        req.config.calc_exp_score_only = doc["calc_exp_score_only"].GetBool();
    }
    if (doc.HasMember("ron_rate")) {
        req.config.ron_rate = doc["ron_rate"].GetDouble();
    }
    if (doc.HasMember("remaining_tiles")) {
        req.config.remaining_tiles = doc["remaining_tiles"].GetInt();
    }
    if (doc.HasMember("enable_other_win_stop")) {
        req.config.enable_other_win_stop = doc["enable_other_win_stop"].GetBool();
    }
    if (doc.HasMember("other_win_hazard")) {
        const auto &hazards = doc["other_win_hazard"].GetArray();
        for (rapidjson::SizeType i = 0; i < hazards.Size(); ++i) {
            req.config.other_win_hazard[i + 1] = hazards[i].GetDouble();
        }
        req.config.other_win_hazard[18] = req.config.other_win_hazard[17];
    }
    if (doc.HasMember("calc_yaku_stats")) {
        req.config.calc_yaku_stats = doc["calc_yaku_stats"].GetBool();
    }
    if (doc.HasMember("calc_shapley_stats")) {
        req.config.calc_shapley_stats = doc["calc_shapley_stats"].GetBool();
    }
    if (doc.HasMember("yaku_filter")) {
        req.config.yaku_filter = doc["yaku_filter"].GetUint64();
    }
    if (doc.HasMember("state_tag")) {
        req.config.state_tag = static_cast<std::uint8_t>(doc["state_tag"].GetUint());
    }

    if (doc.HasMember("wall")) {
        for (int i = 0; i < 37; ++i) {
            req.wall[i] = doc["wall"][i].GetInt();
        }
    }
    else {
        req.wall = create_wall(req.table_config, req.table_state, req.player,
                               req.config.enable_reddora);
    }

    if (doc.HasMember("ip")) {
        req.ip = doc["ip"].GetString();
    }

    if (doc.HasMember("version")) {
        req.version = doc["version"].GetString();
    }

    return req;
}

void validate_tile_counts(const Request &req)
{
    MergedCount wall = create_wall(req.table_config, req.table_state, req.player,
                                   req.config.enable_reddora);

    for (int i = 0; i < 37; ++i) {
        if (wall[i] < 0) {
            throw std::runtime_error(
                fmt::format("Too many tiles are used: tile={}, count={}", Tile::name(i),
                            4 - wall[i]));
        }
    }

    for (int i = 0; i < 37; ++i) {
        if (req.wall[i] > wall[i]) {
            throw std::runtime_error(
                fmt::format("More tiles are requested than remain in the wall: "
                            "tile={}, wall={}, used={}",
                            Tile::name(i), req.wall[i], 4 - wall[i]));
        }
    }

    int total_count = req.player.num_tiles() + req.player.num_melds() * 3;
    if (total_count % 3 == 0 || total_count > 14) {
        throw std::runtime_error("Invalid tile count.");
    }
}

bool is_same_tile_meld(const Meld &meld)
{
    if (meld.tiles.empty()) {
        return false;
    }

    const int tile = Tile::to_normal(meld.tiles.front());
    return std::all_of(meld.tiles.begin(), meld.tiles.end(),
                       [tile](const int x) { return Tile::to_normal(x) == tile; });
}

bool is_chow_meld(const Meld &meld)
{
    if (meld.tiles.size() != 3) {
        return false;
    }

    std::vector<int> tiles;
    tiles.reserve(meld.tiles.size());
    for (const auto tile : meld.tiles) {
        tiles.push_back(Tile::to_normal(tile));
    }
    std::sort(tiles.begin(), tiles.end());

    return Tile::is_suit(tiles[0]) && Tile::is_suit(tiles[1]) &&
           Tile::is_suit(tiles[2]) &&
           ((Tile::is_manzu(tiles[0]) && Tile::is_manzu(tiles[1]) &&
             Tile::is_manzu(tiles[2])) ||
            (Tile::is_pinzu(tiles[0]) && Tile::is_pinzu(tiles[1]) &&
             Tile::is_pinzu(tiles[2])) ||
            (Tile::is_souzu(tiles[0]) && Tile::is_souzu(tiles[1]) &&
             Tile::is_souzu(tiles[2]))) &&
           tiles[1] == tiles[0] + 1 && tiles[2] == tiles[1] + 1;
}

void validate_melds(const Request &req)
{
    for (const auto &meld : req.player.melds) {
        switch (meld.type) {
        case MeldType::Pon:
            if (meld.tiles.size() != 3 || !is_same_tile_meld(meld)) {
                throw std::runtime_error("Invalid pon meld.");
            }
            break;
        case MeldType::Chi:
            if (!is_chow_meld(meld)) {
                throw std::runtime_error("Invalid chow meld.");
            }
            break;
        case MeldType::Ankan:
        case MeldType::Daiminkan:
        case MeldType::Kakan:
            if (meld.tiles.size() != 4 || !is_same_tile_meld(meld)) {
                throw std::runtime_error("Invalid kong meld.");
            }
            break;
        default:
            throw std::runtime_error(
                fmt::format("Invalid meld type: type={}.", meld.type));
        }
    }
}

void validate_sanma_tiles(const Request &req)
{
    if (req.table_config.game_mode != GameMode::Sanma) {
        if (req.player.nuki_count > 0) {
            throw std::runtime_error("Nuki dora is only allowed in sanma.");
        }
        return;
    }

    if (has_sanma_disabled_tiles(req.player.hand)) {
        throw std::runtime_error("Sanma hand contains disabled tiles.");
    }

    for (const auto &meld : req.player.melds) {
        if (meld.type == MeldType::Chi) {
            throw std::runtime_error("Sanma hand contains a chow meld.");
        }
        for (const auto tile : meld.tiles) {
            if (Tile::is_sanma_disabled(tile)) {
                throw std::runtime_error(fmt::format(
                    "Sanma meld contains a disabled tile: tile={}.", Tile::name(tile)));
            }
        }
    }

    for (const auto tile : req.table_state.dora_indicators) {
        if (Tile::is_sanma_disabled(tile)) {
            throw std::runtime_error(
                fmt::format("Sanma dora indicator contains a disabled tile: tile={}.",
                            Tile::name(tile)));
        }
    }

    for (int tile = 0; tile < Tile::Length; ++tile) {
        if (Tile::is_sanma_disabled(tile) && req.wall[tile] > 0) {
            throw std::runtime_error(
                fmt::format("Sanma wall contains disabled tiles: tile={}, count={}.",
                            Tile::name(tile), req.wall[tile]));
        }
    }
}

rapidjson::Value
serialize_necessary_tiles(const std::vector<std::tuple<int, int>> &tiles,
                          rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();
    rapidjson::Value value(rapidjson::kArrayType);
    for (const auto [tile, count] : tiles) {
        rapidjson::Value x(rapidjson::kObjectType);
        x.AddMember("tile", tile, allocator);
        x.AddMember("count", count, allocator);
        value.PushBack(x, allocator);
    }

    return value;
}

rapidjson::Value
serialize_expected_score(const std::vector<ExpectedScoreCalculator::Stat> &stats,
                         rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();

    rapidjson::Value value(rapidjson::kArrayType);
    for (const auto &stat : stats) {
        rapidjson::Value x(rapidjson::kObjectType);

        x.AddMember("tile", stat.tile, allocator);

        rapidjson::Value tenpai_prob(rapidjson::kArrayType);
        for (const auto prob : stat.tenpai_prob) {
            tenpai_prob.PushBack(std::clamp(prob, 0.0, 1.0), allocator);
        }
        x.AddMember("tenpai_prob", tenpai_prob, allocator);

        rapidjson::Value win_prob(rapidjson::kArrayType);
        for (const auto prob : stat.win_prob) {
            win_prob.PushBack(std::clamp(prob, 0.0, 1.0), allocator);
        }
        x.AddMember("win_prob", win_prob, allocator);

        rapidjson::Value exp_score(rapidjson::kArrayType);
        for (const auto value : stat.exp_score) {
            exp_score.PushBack(value, allocator);
        }
        x.AddMember("exp_score", exp_score, allocator);

        rapidjson::Value call_prob(rapidjson::kArrayType);
        for (const auto prob : stat.call_prob) {
            call_prob.PushBack(std::clamp(prob, 0.0, 1.0), allocator);
        }
        x.AddMember("call_prob", call_prob, allocator);

        rapidjson::Value call_win_prob(rapidjson::kArrayType);
        for (const auto prob : stat.call_win_prob) {
            call_win_prob.PushBack(std::clamp(prob, 0.0, 1.0), allocator);
        }
        x.AddMember("call_win_prob", call_win_prob, allocator);

        rapidjson::Value call_tile_stats(rapidjson::kArrayType);
        for (const auto &entry : stat.call_tile_stats) {
            rapidjson::Value call_tile(rapidjson::kObjectType);
            call_tile.AddMember("tile", entry.tile, allocator);
            rapidjson::Value probability(rapidjson::kArrayType);
            for (const auto prob : entry.probability) {
                probability.PushBack(std::clamp(prob, 0.0, 1.0), allocator);
            }
            call_tile.AddMember("probability", probability, allocator);
            call_tile_stats.PushBack(call_tile, allocator);
        }
        x.AddMember("call_tile_stats", call_tile_stats, allocator);

        x.AddMember("necessary_tiles",
                    serialize_necessary_tiles(stat.necessary_tiles, doc), allocator);

        x.AddMember("shanten", stat.shanten, allocator);

        if (!stat.yaku_stats.empty()) {
            rapidjson::Value yaku_stats(rapidjson::kArrayType);
            for (const auto &entry : stat.yaku_stats) {
                rapidjson::Value yaku(rapidjson::kObjectType);
                yaku.AddMember("yaku", entry.yaku, allocator);
                rapidjson::Value occurrence(rapidjson::kArrayType);
                for (const double probability : entry.occurrence_prob) {
                    occurrence.PushBack(std::clamp(probability, 0.0, 1.0), allocator);
                }
                yaku.AddMember("occurrence_prob", occurrence, allocator);
                rapidjson::Value shapley(rapidjson::kArrayType);
                for (const double score : entry.shapley_score) {
                    shapley.PushBack(score, allocator);
                }
                yaku.AddMember("shapley_score", shapley, allocator);
                rapidjson::Value called_occurrence(rapidjson::kArrayType);
                for (const double probability : entry.called_occurrence_prob) {
                    called_occurrence.PushBack(std::clamp(probability, 0.0, 1.0),
                                               allocator);
                }
                yaku.AddMember("called_occurrence_prob", called_occurrence, allocator);
                rapidjson::Value called_shapley(rapidjson::kArrayType);
                for (const double score : entry.called_shapley_score) {
                    called_shapley.PushBack(score, allocator);
                }
                yaku.AddMember("called_shapley_score", called_shapley, allocator);
                yaku_stats.PushBack(yaku, allocator);
            }
            x.AddMember("yaku_stats", yaku_stats, allocator);
        }

        value.PushBack(x, allocator);
    }

    return value;
}

rapidjson::Value serialize_string(const std::string &str, rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();
    rapidjson::Value value;
    value.SetString(str.c_str(), static_cast<rapidjson::SizeType>(str.length()),
                    allocator);

    return value;
}

rapidjson::Value serialize_input(const Request &req, rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();
    rapidjson::Value input_val(rapidjson::kObjectType);

    input_val.AddMember("game_mode", static_cast<int>(req.table_config.game_mode),
                        allocator);
    input_val.AddMember("round_wind", req.round_state.round_wind, allocator);
    input_val.AddMember("seat_wind", req.player.seat_wind, allocator);

    rapidjson::Value dora_indicators(rapidjson::kArrayType);
    for (const auto tile : req.table_state.dora_indicators) {
        dora_indicators.PushBack(tile, allocator);
    }
    input_val.AddMember("dora_indicators", dora_indicators, allocator);

    rapidjson::Value hand(rapidjson::kArrayType);
    for (int i = 0; i < 37; ++i) {
        int count = req.player.hand[i];
        if (i == Tile::Manzu5) {
            count -= req.player.hand[Tile::RedManzu5];
        }
        else if (i == Tile::Pinzu5) {
            count -= req.player.hand[Tile::RedPinzu5];
        }
        else if (i == Tile::Souzu5) {
            count -= req.player.hand[Tile::RedSouzu5];
        }
        for (int j = 0; j < count; ++j) {
            hand.PushBack(i, allocator);
        }
    }
    input_val.AddMember("hand", hand, allocator);

    rapidjson::Value melds(rapidjson::kArrayType);
    for (const auto &meld : req.player.melds) {
        rapidjson::Value meld_val(rapidjson::kObjectType);
        meld_val.AddMember("type", meld.type, allocator);

        rapidjson::Value tiles(rapidjson::kArrayType);
        for (const auto tile : meld.tiles) {
            tiles.PushBack(tile, allocator);
        }
        meld_val.AddMember("tiles", tiles, allocator);

        melds.PushBack(meld_val, allocator);
    }
    input_val.AddMember("melds", melds, allocator);

    input_val.AddMember("nuki_count", req.player.nuki_count, allocator);

    rapidjson::Value wall(rapidjson::kArrayType);
    for (const auto count : req.wall) {
        wall.PushBack(count, allocator);
    }
    input_val.AddMember("wall", wall, allocator);

    return input_val;
}

} // namespace

/**
 * @brief Convert a JSON document to a string.
 *
 * @param[in] doc JSON document to serialize.
 * @return Serialized JSON string.
 */
std::string dump_json(const rapidjson::Document &doc)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.SetMaxDecimalPlaces(4);
    doc.Accept(writer);

    return buffer.GetString();
}

/**
 * @brief Parse and validate a request JSON string.
 *
 * @param[in] json Request JSON string.
 * @param[out] doc Parsed JSON document.
 * @throw std::runtime_error If parsing, schema validation, or version check fails.
 */
void parse_json(const std::string &json, rapidjson::Document &doc)
{
    if (doc.Parse(json.c_str()).HasParseError()) {
        throw std::runtime_error("Failed to parse JSON string: invalid JSON format.");
    }

    // Validate JSON schema.
    rapidjson::SchemaValidator validator(get_request_schema());
    if (!doc.Accept(validator)) {
        rapidjson::StringBuffer sb;
        validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
        const std::string invalid_schema = sb.GetString();
        const std::string invalid_keyword = validator.GetInvalidSchemaKeyword();
        sb.Clear();
        validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
        const std::string invalid_doc = sb.GetString();

        throw std::runtime_error(fmt::format(
            u8"JSON schema validation failed: schema={}, keyword={}, doc={}. "
            "ページを更新してから、もう一度お試しください。",
            invalid_schema, invalid_keyword, invalid_doc));
    }

    if (std::strcmp(doc["version"].GetString(), PROJECT_VERSION) != 0) {
        throw std::runtime_error(
            fmt::format(u8"Request version mismatch: expected={}, actual={}. "
                        "ページを更新してから、もう一度お試しください。",
                        PROJECT_VERSION, doc["version"].GetString()));
    }
}

/**
 * @brief Deserialize and validate a request from a parsed JSON document.
 *
 * @param[in] doc Parsed request JSON document.
 * @return Validated request object.
 * @throw std::runtime_error If the request content is invalid.
 */
Request deserialize_request(const rapidjson::Document &doc)
{
    Request req = make_request(doc);

    validate_melds(req);
    validate_sanma_tiles(req);
    validate_tile_counts(req);

    return req;
}

/**
 * @brief Build a success response JSON document from a request.
 *
 * @param[in] req Validated request object.
 * @param[in] result Calculated result to serialize.
 * @param[in,out] doc Response document to populate.
 */
void build_success_response(const Request &req, const CalculationResult &result,
                            rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();
    doc.SetObject();
    doc.AddMember("success", true, allocator);
    doc.AddMember("engine_version", serialize_string(PROJECT_VERSION, doc), allocator);
    doc.AddMember("api_version", 1, allocator);
    doc.AddMember("input", serialize_input(req, doc), allocator);

    rapidjson::Value shanten_val(rapidjson::kObjectType);
    shanten_val.AddMember("all", result.shanten, allocator);
    shanten_val.AddMember("regular", result.regular_shanten, allocator);
    shanten_val.AddMember("seven_pairs", result.seven_pairs_shanten, allocator);
    shanten_val.AddMember("thirteen_orphans", result.thirteen_orphans_shanten,
                          allocator);
    doc.AddMember("shanten", shanten_val, allocator);
    doc.AddMember("stats", serialize_expected_score(result.stats, doc), allocator);
    doc.AddMember("searched", result.searched, allocator);
    doc.AddMember("time", static_cast<int64_t>(result.time_us), allocator);

    rapidjson::Value profile(rapidjson::kObjectType);
    profile.AddMember("graph_build_us",
                      static_cast<int64_t>(result.profile.graph_build_us), allocator);
    profile.AddMember("csr_build_us",
                      static_cast<int64_t>(result.profile.csr_build_us), allocator);
    profile.AddMember("dp_us", static_cast<int64_t>(result.profile.dp_us), allocator);
    profile.AddMember("draw_vertices", result.profile.draw_vertices, allocator);
    profile.AddMember("discard_vertices", result.profile.discard_vertices, allocator);
    profile.AddMember("edges", result.profile.edges, allocator);
    profile.AddMember("necessary_tile_calculator_calls",
                      result.profile.necessary_tile_calculator_calls, allocator);
    profile.AddMember("unnecessary_tile_calculator_calls",
                      result.profile.unnecessary_tile_calculator_calls, allocator);
    rapidjson::Value core_invocations(rapidjson::kArrayType);
    for (const auto &core : result.profile.core_invocations) {
        rapidjson::Value entry(rapidjson::kObjectType);
        entry.AddMember("graph_build_us", static_cast<int64_t>(core.graph_build_us),
                        allocator);
        entry.AddMember("csr_build_us", static_cast<int64_t>(core.csr_build_us),
                        allocator);
        entry.AddMember("dp_us", static_cast<int64_t>(core.dp_us), allocator);
        entry.AddMember("draw_vertices", core.draw_vertices, allocator);
        entry.AddMember("discard_vertices", core.discard_vertices, allocator);
        entry.AddMember("edges", core.edges, allocator);
        core_invocations.PushBack(entry, allocator);
    }
    profile.AddMember("core_invocations", core_invocations, allocator);
    profile.AddMember("merge_turn_yaku_overlay_us",
                      static_cast<int64_t>(result.profile.merge_turn_yaku_overlay_us),
                      allocator);
    doc.AddMember("profile", profile, allocator);

    rapidjson::Value config_val(rapidjson::kObjectType);
    config_val.AddMember("enable_reddora", result.config.enable_reddora, allocator);
    config_val.AddMember("enable_uradora", result.config.enable_uradora, allocator);
    config_val.AddMember("enable_shanten_down", result.config.enable_shanten_down,
                         allocator);
    config_val.AddMember("enable_tegawari", result.config.enable_tegawari, allocator);
    config_val.AddMember("auto_disable_deep_search",
                         result.config.auto_disable_deep_search, allocator);
    config_val.AddMember("enable_riichi", result.config.enable_riichi, allocator);
    if (result.config.enable_calls) {
        config_val.AddMember("enable_calls", true, allocator);
    }
    if (result.config.enable_turn_yaku) {
        config_val.AddMember("enable_turn_yaku", true, allocator);
    }
    config_val.AddMember("t_min", result.config.t_min, allocator);
    config_val.AddMember("t_max", result.config.t_max, allocator);
    config_val.AddMember("sum", result.config.sum, allocator);
    config_val.AddMember("extra", result.config.extra, allocator);
    config_val.AddMember("shanten_type", result.config.shanten_type, allocator);
    config_val.AddMember("calc_stats", result.config.calc_stats, allocator);
    if (result.config.calc_exp_score_only) {
        config_val.AddMember("calc_exp_score_only", true, allocator);
    }
    if (result.config.ron_rate != 0.0) {
        config_val.AddMember("ron_rate", result.config.ron_rate, allocator);
    }
    if (result.config.remaining_tiles >= 0) {
        config_val.AddMember("remaining_tiles", result.config.remaining_tiles,
                             allocator);
    }
    if (result.config.enable_other_win_stop) {
        config_val.AddMember("enable_other_win_stop", true, allocator);
        rapidjson::Value other_win_hazard(rapidjson::kArrayType);
        for (int turn = 1; turn <= 18; ++turn) {
            other_win_hazard.PushBack(result.config.other_win_hazard[turn], allocator);
        }
        config_val.AddMember("other_win_hazard", other_win_hazard, allocator);
    }
    if (result.config.calc_yaku_stats) {
        config_val.AddMember("calc_yaku_stats", true, allocator);
    }
    if (result.config.calc_shapley_stats) {
        config_val.AddMember("calc_shapley_stats", true, allocator);
    }
    if (result.config.calc_yaku_stats || result.config.calc_shapley_stats) {
        config_val.AddMember("yaku_filter", result.config.yaku_filter, allocator);
    }
    if (result.config.state_tag != 0) {
        config_val.AddMember("state_tag", result.config.state_tag, allocator);
    }
    config_val.AddMember(
        "num_tiles", req.player.num_tiles() + req.player.num_melds() * 3, allocator);
    doc.AddMember("config", config_val, allocator);
}

/**
 * @brief Build an error response JSON document.
 *
 * @param[in] message Error message to serialize.
 * @param[in,out] doc Response document to populate.
 */
void build_error_response(const std::string &message, rapidjson::Document &doc)
{
    auto &allocator = doc.GetAllocator();
    doc.SetObject();
    doc.AddMember("success", false, allocator);
    doc.AddMember("err_msg", serialize_string(message, doc), allocator);
}
