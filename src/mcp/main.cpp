#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include <atp/mcp/catalog_tools.hpp>
#include <atp/mcp/document_tools.hpp>
#include <atp/mcp/execution_tools.hpp>
#include <atp/mcp/json_rpc.hpp>
#include <atp/mcp/options.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/resources.hpp>
#include <atp/mcp/server.hpp>
#include <atp/mcp/settings_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>

namespace {

/// Takes the real stdout for the protocol and points the process's own stdout at stderr.
///
/// A module is entitled to print — the demo plugin's printer module does exactly that — and it runs
/// inside this process. One such line in the protocol stream desynchronises the client, and the
/// modules cannot be asked to know that. So the descriptor is redirected once, at startup: anything
/// written through std::cout or printf from here on lands on stderr, and the protocol keeps a
/// private handle to the original stream.
/// @return the stream every response is written to; never null
/// @throws std::runtime_error if the descriptor cannot be duplicated
[[nodiscard]] std::FILE* claim_protocol_stream() {
#if defined(_WIN32)
    const int duplicated = _dup(_fileno(stdout));
    (void)_dup2(_fileno(stderr), _fileno(stdout));
    std::FILE* protocol = duplicated >= 0 ? _fdopen(duplicated, "w") : nullptr;
#else
    const int duplicated = dup(fileno(stdout));
    (void)dup2(fileno(stderr), fileno(stdout));
    std::FILE* protocol = duplicated >= 0 ? fdopen(duplicated, "w") : nullptr;
#endif
    if (protocol == nullptr) {
        throw std::runtime_error("cannot take stdout over for the protocol stream");
    }
    return protocol;
}

}  // namespace

int main(int argc, char** argv) {
    // Argument parsing happens before anything else, so bad invocation exits with 2 rather than
    // leaving a half-built server talking on stdout.
    std::optional<atp::mcp::options> args = atp::mcp::parse_options(argc, argv);
    if (!args) {
        std::cerr << atp::mcp::usage;
        return 2;
    }

    try {
        std::FILE* protocol = claim_protocol_stream();
        atp::mcp::workspace ws(std::move(args->root), std::move(args->plugin_dirs), std::move(args->scan_dirs));
        atp::mcp::tool_registry tools;
        atp::mcp::resource_registry resources;
        atp::mcp::register_catalog_tools(tools, ws);
        atp::mcp::register_document_tools(tools, ws);
        atp::mcp::register_settings_tools(tools, ws);
        atp::mcp::register_execution_tools(tools, ws);
        atp::mcp::register_resources(resources, ws);
        atp::mcp::server server(tools, resources);

        // The protocol stream carries one JSON object per line, no embedded newlines. Diagnostics —
        // ours and the modules' alike — go to stderr; see claim_protocol_stream above.
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }
            std::optional<nlohmann::json> reply;
            try {
                reply = server.handle(nlohmann::json::parse(line));
            } catch (const nlohmann::json::parse_error& e) {
                reply = atp::mcp::make_error(nullptr, atp::mcp::rpc_parse_error, e.what());
            }
            if (reply) {
                const std::string encoded = reply->dump();
                std::fwrite(encoded.data(), 1, encoded.size(), protocol);
                std::fputc('\n', protocol);
                std::fflush(protocol);
            }
        }
        // A closed stdin is how the client shuts the server down; the session ends with it.
        ws.run_session().stop();
    } catch (const std::exception& e) {
        std::cerr << "atp_mcp: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
