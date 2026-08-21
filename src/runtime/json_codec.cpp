// SPDX-License-Identifier: Apache-2.0
#include <atp/runtime/json_codec.hpp>

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>
#include <atp/runtime/config_error.hpp>
#include <atp/runtime/config_value_json.hpp>

namespace atp::runtime {

atp::config::node json_parse(std::string_view text) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::exception& e) {
        throw config_error(std::string("cannot parse JSON: ") + e.what());
    }
    try {
        return to_config_value(parsed);
    } catch (const atp::config::access_error& e) {
        throw config_error(e.what());
    }
}

std::optional<atp::config::node> try_json_parse(std::string_view text) {
    try {
        return to_config_value(nlohmann::json::parse(text.begin(), text.end()));
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    } catch (const atp::config::access_error&) {
        return std::nullopt;
    }
}

std::string json_dump(const atp::config::node& value, int indent) {
    return to_json_value(value).dump(indent);
}

}  // namespace atp::runtime
