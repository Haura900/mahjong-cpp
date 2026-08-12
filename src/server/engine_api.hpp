#ifndef MAHJONG_CPP_ENGINE_API_H
#define MAHJONG_CPP_ENGINE_API_H

#include <functional>
#include <string>

#include "json_parser.hpp"

using RequestObserver = std::function<void(const Request &)>;

// Platform-neutral JSON API shared by the native server and WebAssembly build.
std::string process_engine_request(const std::string &json,
                                   const RequestObserver &observer = {});

#endif // MAHJONG_CPP_ENGINE_API_H
