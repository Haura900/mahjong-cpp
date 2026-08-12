#include "engine_api.hpp"

#include "request_processor.hpp"

#include <rapidjson/document.h>

std::string process_engine_request(const std::string &json,
                                   const RequestObserver &observer)
{
    rapidjson::Document req_doc;
    rapidjson::Document res_doc;
    res_doc.SetObject();

    try {
        parse_json(json, req_doc);
        Request req = deserialize_request(req_doc);
        if (observer) {
            observer(req);
        }
        const CalculationResult result = calculate_result(req);
        build_success_response(req, result, res_doc);
    }
    catch (const std::exception &e) {
        build_error_response(e.what(), res_doc);
    }

    return dump_json(res_doc);
}
