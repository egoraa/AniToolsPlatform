#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_loader.hpp>
#include <atp/runtime/config_model.hpp>
#include <atp/runtime/config_validator.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/runtime/property_override.hpp>

namespace {

// Плоский флаг — единственное, что разрешено сигнальному обработчику.
volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

constexpr const char* usage = "usage: atp_app <config.json> [-p path.prop=value]...\n";

}  // namespace

int main(int argc, char** argv) {
    // Разбор аргументов — до основного try: неверный ввод пользователя даёт
    // код 2, как и невалидный конфиг, а не 1 (аварию пайплайна).
    std::filesystem::path config_path;
    std::vector<atp::runtime::property_override> overrides;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "-p") {
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
        const nlohmann::json doc = atp::runtime::load_config(config_path);
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

        // Overrides поверх конфига — до start: initialize зовётся из
        // runner.start, модуль видит значения уже в initialize. Отказ —
        // ошибка ввода, а не аварии: код 2, как у невалидного конфига.
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
