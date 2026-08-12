#include <string>

#include <emscripten/bind.h>

#include "server/engine_api.hpp"

namespace
{

std::string analyze_json(const std::string &input)
{
    return process_engine_request(input);
}

std::string engine_version()
{
    return PROJECT_VERSION;
}

} // namespace

EMSCRIPTEN_BINDINGS(mahjong_wasm)
{
    emscripten::function("analyzeJson", &analyze_json);
    emscripten::function("engineVersion", &engine_version);
}
