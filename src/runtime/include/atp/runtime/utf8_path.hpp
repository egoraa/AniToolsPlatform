// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_UTF8_PATH_HPP
#define ATP_RUNTIME_UTF8_PATH_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace atp::runtime {

/// A path as UTF-8, never through path::string().
///
/// On Windows that conversion goes through the process code page and throws for anything it cannot
/// represent, so a config file named in Cyrillic would take down the read with an exception about
/// encoding instead of a message about the file.
[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

[[nodiscard]] inline std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}  // namespace atp::runtime

#endif
