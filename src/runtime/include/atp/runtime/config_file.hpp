// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_FILE_HPP
#define ATP_RUNTIME_CONFIG_FILE_HPP

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <atp/config/node.hpp>
#include <atp/module/module_config.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/runtime/utf8_path.hpp>

namespace atp::runtime {

/// The prefix that tells a "file:" source from the default one, which is a name in "configs".
inline constexpr std::string_view config_file_prefix = "file:";

/// Reads a module config from a file named by a "file:" string.
///
/// The extension decides the format, and that is a diagnosis rather than a convenience: a .json is
/// required to parse, so a forgotten comma becomes an error naming the position instead of a silent
/// slide into "unknown format" that would hand the module an empty config. Anything else — including
/// no extension at all — is opaque text the module parses itself, so the host learns no new formats.
///
/// Bytes are taken verbatim, BOM included (the parser skips one itself), and neither validated nor
/// transcoded: the text is declared UTF-8 like every other string here.
///
/// @param spec path as written after "file:", relative to @p base_dir or absolute
/// @param base_dir directory of the document the string was written in; empty when the caller has
///        none, which is a hard error for a relative path and no obstacle for an absolute one
/// @throws config_error naming the resolved path for every failure: no base directory, an unreadable
///         file, a directory, unparsable JSON, or a parsed root that is neither object nor null
[[nodiscard]] inline module_config load_module_config(std::string_view spec, const std::filesystem::path& base_dir) {
    const std::filesystem::path named = path_from_utf8(spec);
    if (named.empty()) {
        throw config_error("a config file was named as 'file:' with no path");
    }
    if (named.is_relative() && base_dir.empty()) {
        throw config_error("config file '" + std::string(spec) +
                           "' is relative, so it needs the document's directory, and there is none");
    }
    const std::filesystem::path file = named.is_absolute() ? named : base_dir / named;
    const std::string shown = path_to_utf8(file);
    if (std::filesystem::is_directory(file)) {
        throw config_error("config file '" + shown + "' is a directory");
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw config_error("cannot open config file '" + shown + "'");
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string extension = path_to_utf8(file.extension());
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (extension != ".json") {
        return module_config::opaque(std::move(text), shown);
    }
    atp::config::node parsed;
    try {
        parsed = json_parse(text);
    } catch (const config_error& e) {
        throw config_error("cannot parse config file '" + shown + "': " + e.what());
    }
    if (!parsed.is_object() && !parsed.is_null()) {
        throw config_error("the root of a config must be an object, but '" + shown + "' holds a " +
                           std::string(atp::config::node::kind_name(parsed.kind())));
    }
    return module_config(std::move(parsed), std::move(text), shown);
}

}  // namespace atp::runtime

#endif
