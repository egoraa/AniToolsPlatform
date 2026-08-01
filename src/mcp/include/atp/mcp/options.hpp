#ifndef ATP_MCP_OPTIONS_HPP
#define ATP_MCP_OPTIONS_HPP

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace atp::mcp {

/// The server's command line, parsed. Directory values are taken as written and resolved against the
/// process's working directory, not against the root: they come from the operator who started the
/// server, unlike the client-supplied paths the workspace confines.
struct options {
    std::filesystem::path root = std::filesystem::current_path();
    std::vector<std::filesystem::path> plugin_dirs;
    std::vector<std::filesystem::path> scan_dirs;
};

/// One line describing the accepted command line.
inline constexpr std::string_view usage =
    "usage: atp_mcp [--root <dir>] [--plugin-dir <dir>]... [--scan-dir <dir>]...\n";

/// Parses the process arguments.
/// @param argc argument count as main received it
/// @param argv argument vector as main received it; argv[0] is skipped
/// @return the parsed options, or nullopt if an argument is unknown or a flag has no value
[[nodiscard]] inline std::optional<options> parse_options(int argc, const char* const* argv) {
    options parsed;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            parsed.root = argv[++i];
        } else if (arg == "--plugin-dir" && i + 1 < argc) {
            parsed.plugin_dirs.emplace_back(argv[++i]);
        } else if (arg == "--scan-dir" && i + 1 < argc) {
            parsed.scan_dirs.emplace_back(argv[++i]);
        } else {
            return std::nullopt;
        }
    }
    return parsed;
}

}  // namespace atp::mcp

#endif
