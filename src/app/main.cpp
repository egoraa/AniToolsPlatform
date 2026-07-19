#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/app/config_loader.hpp>
#include <atp/app/config_model.hpp>
#include <atp/app/config_validator.hpp>
#include <atp/app/pipeline_builder.hpp>

namespace {

// Плоский флаг — единственное, что разрешено сигнальному обработчику.
volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: atp_app <config.json>\n";
        return 2;
    }
    try {
        const std::filesystem::path config_path = argv[1];
        const nlohmann::json doc = atp::app::load_config(config_path);
        const std::vector<std::string> errors = atp::app::validate(doc);
        if (!errors.empty()) {
            std::cerr << "invalid config '" << config_path.string() << "':\n";
            for (const std::string& e : errors) {
                std::cerr << "  " << e << '\n';
            }
            return 2;
        }
        const atp::app::config cfg = atp::app::decode(doc);

        atp::app::application app;
        atp::app::build(app, cfg, std::filesystem::weakly_canonical(config_path).parent_path());

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        app.runner.start(app.pipe);
        std::cout << "pipeline is running; press Ctrl+C to stop\n";
        // Опрос вместо runner.wait(): wait блокируется до аварии, а нужен
        // ещё и выход по Ctrl+C. Управление раннером — только этот поток
        // (owner-thread-only контракт), опрос error() безопасен.
        while (g_stop == 0 && app.runner.error() == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        app.runner.stop();
        if (std::exception_ptr e = app.runner.error()) {
            std::rethrow_exception(e);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "atp_app: " << e.what() << '\n';
        return 1;
    }
}
