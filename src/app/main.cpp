// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/mcp/application_control.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/server.hpp>
#include <atp/mcp/socket_server.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/runtime/command_queue.hpp>
#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/console_encoding.hpp>
#include <atp/runtime/group.hpp>
#include <atp/runtime/log_pump.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/property_override.hpp>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

void print_metrics(const atp::runtime::group& root) {
    std::vector<atp::runtime::group::module_stats> stats = root.metrics();
    std::ranges::sort(stats, [](const atp::runtime::group::module_stats& a,
                                const atp::runtime::group::module_stats& b) { return a.total > b.total; });
    std::cout << "\nmodule                              calls     busy      total ms       max us\n";
    for (const atp::runtime::group::module_stats& s : stats) {
        std::cout << std::left << std::setw(32) << s.path << std::right << std::setw(10) << s.calls << std::setw(9)
                  << s.busy_calls << std::setw(13) << std::chrono::duration<double, std::milli>(s.total).count()
                  << std::setw(13) << std::chrono::duration<double, std::micro>(s.max).count() << '\n';
    }
}

void print_input_metrics(const atp::runtime::group& root) {
    std::vector<atp::runtime::group::port_stats> ports = root.input_metrics();
    std::ranges::sort(ports, [](const atp::runtime::group::port_stats& a, const atp::runtime::group::port_stats& b) {
        return a.stats.discarded > b.stats.discarded;
    });
    std::cout << "\nport                              received    discarded     pending        peak    capacity\n";
    for (const atp::runtime::group::port_stats& p : ports) {
        std::cout << std::left << std::setw(32) << p.path << std::right << std::setw(10) << p.stats.received
                  << std::setw(13) << p.stats.discarded << std::setw(12) << p.stats.pending << std::setw(12)
                  << p.stats.peak_pending << std::setw(12) << p.stats.capacity << '\n';
    }
}

constexpr const char* usage =
    "usage: atp_app <config.json> [-p path.prop=value]... [--metrics] [--run-for <ms>] [--control <port>] "
    "[--log <level>]\n";

}  // namespace

int main(int argc, char** argv) {
    const atp::runtime::console_utf8 console;
    std::filesystem::path config_path;
    std::vector<atp::runtime::property_override> overrides;
    bool metrics = false;
    std::chrono::milliseconds run_for{0};
    std::optional<std::uint16_t> control_port;
    atp::log_level log_threshold = atp::log_level::info;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--metrics") {
                metrics = true;
            } else if (arg == "--run-for") {
                if (i + 1 == argc) {
                    std::cerr << usage;
                    return 2;
                }
                run_for = std::chrono::milliseconds(std::stoll(argv[++i]));
            } else if (arg == "--control") {
                if (i + 1 == argc) {
                    std::cerr << usage;
                    return 2;
                }
                control_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
            } else if (arg == "--log") {
                if (i + 1 == argc) {
                    std::cerr << usage;
                    return 2;
                }
                const std::optional<atp::log_level> level = atp::runtime::level_from_name(argv[++i]);
                if (!level) {
                    std::cerr << usage;
                    return 2;
                }
                log_threshold = *level;
            } else if (arg == "-p") {
                if (i + 1 == argc) {
                    std::cerr << usage;
                    return 2;
                }
                overrides.push_back(atp::runtime::parse_property_override(argv[++i]));
            } else if (config_path.empty()) {
                config_path = argv[i];
            } else {
                std::cerr << usage;
                return 2;
            }
        }
    } catch (const atp::runtime::config_error& e) {
        std::cerr << "atp_app: " << e.what() << '\n';
        return 2;
    }
    if (config_path.empty()) {
        std::cerr << usage;
        return 2;
    }
    try {
        const atp::config::node doc = atp::runtime::load_config(config_path);
        const std::vector<std::string> errors = atp::runtime::validate(doc);
        if (!errors.empty()) {
            std::cerr << "invalid config '" << config_path.string() << "':\n";
            for (const std::string& e : errors) {
                std::cerr << "  " << e << '\n';
            }
            return 2;
        }
        const atp::runtime::config cfg = atp::runtime::decode(doc);

        atp::runtime::application app;
        atp::runtime::build(app, cfg, std::filesystem::weakly_canonical(config_path).parent_path());

        try {
            for (const atp::runtime::property_override& o : overrides) {
                atp::runtime::apply_property_override(app.pipe.root(), o);
            }
        } catch (const atp::runtime::config_error& e) {
            std::cerr << "atp_app: " << e.what() << '\n';
            return 2;
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        atp::runtime::log_pump logs(app.pipe, [log_threshold](const atp::runtime::log_line& line) {
            if (line.level <= log_threshold) {
                std::cerr << atp::runtime::format_log_line(line) << '\n';
            }
        });
        app.runner.start(app.pipe);
        if (metrics) {
            app.pipe.root().set_metrics_enabled(true);
        }
        atp::runtime::command_queue queue;
        atp::mcp::application_control live(app);
        atp::mcp::tool_registry tools;
        atp::mcp::resource_registry resources;
        atp::mcp::server rpc(tools, resources);
        std::optional<atp::mcp::socket_server> control;
        if (control_port) {
            atp::mcp::register_control_tools(tools, live);
            atp::mcp::register_shutdown_tool(tools, [] { g_stop = 1; });
            control.emplace(*control_port, [&queue, &rpc](const nlohmann::json& message) {
                return queue.call([&rpc, &message] { return rpc.handle(message); });
            });
            std::cerr << "control channel on 127.0.0.1:" + std::to_string(control->port()) +
                             " (unauthenticated: any local process can drive or stop this host)\n";
        }

        std::cout << "pipeline is running; press Ctrl+C to stop\n";
        const auto deadline = std::chrono::steady_clock::now() + run_for;
        while (g_stop == 0 && app.runner.error() == nullptr) {
            if (run_for > std::chrono::milliseconds::zero() && std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            queue.run_pending(std::chrono::milliseconds(50));
        }
        queue.close();
        if (control) {
            control->stop();
        }
        app.runner.stop();
        if (metrics) {
            print_metrics(app.pipe.root());
            print_input_metrics(app.pipe.root());
        }
        if (std::exception_ptr e = app.runner.error()) {
            std::rethrow_exception(e);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "atp_app: " << e.what() << '\n';
        return 1;
    }
}
