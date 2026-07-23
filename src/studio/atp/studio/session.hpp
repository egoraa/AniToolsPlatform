#ifndef ATP_STUDIO_SESSION_HPP
#define ATP_STUDIO_SESSION_HPP

#include <any>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <atp/runtime/config_model.hpp>
#include <atp/runtime/pipeline_builder.hpp>
#include <atp/group.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

namespace atp::studio {

// Исполнение документа: на каждый запуск — свежие pipeline и runner,
// реестр модулей живёт на уровне сессии (module_manager) и переживает
// запуски. Владелец сессии — поток GUI: он же владелец раннера
// (owner-thread-only контракт соблюдается конструктивно).
class session {
   public:
    explicit session(module_registry& registry) : registry_(&registry) {}

    // Бросает (config_error сборки и т.п.) — сессия при этом остаётся
    // чистой и пригодной к следующему запуску.
    void start(const runtime::config& cfg) {
        if (running()) {
            throw std::logic_error("session is already running");
        }
        auto pipe = std::make_unique<pipeline>();
        auto runner = std::make_unique<pipeline_runner>();
        runtime::build_pipeline(*pipe, *runner, cfg, *registry_);
        runner->start(*pipe);
        pipe_ = std::move(pipe);  // публикация только после удачного старта
        runner_ = std::move(runner);
    }

    void stop() {
        if (runner_) {
            runner_->stop();
        }
    }

    [[nodiscard]] bool running() const {
        return runner_ && runner_->running();
    }

    // Ошибка исполнения (первая); nullptr — не запускались или чисто.
    [[nodiscard]] std::exception_ptr error() const {
        return runner_ ? runner_->error() : nullptr;
    }

    [[nodiscard]] std::vector<pipeline_runner::thread_stats> stats() const {
        return runner_ ? runner_->stats() : std::vector<pipeline_runner::thread_stats>{};
    }

    // Снимок соединений для мониторинга: (путь группы, индекс соединения) —
    // те же, что в документе: build_group сохраняет порядок объявления.
    struct connection_sample {
        std::string group_path;
        std::size_t index = 0;
        std::optional<std::any> value;
        std::uint64_t writes = 0;
    };

    [[nodiscard]] std::vector<connection_sample> sample_connections() const {
        std::vector<connection_sample> out;
        if (pipe_) {
            collect(pipe_->root(), "", out);
        }
        return out;
    }

   private:
    void collect(const group& g, const std::string& path, std::vector<connection_sample>& out) const {
        std::size_t index = 0;
        for (const group::connection& c : g.connections()) {
            out.push_back({path, index, c.out->peek(), c.out->write_count()});
            ++index;
        }
        for (const group::child& child : g.children()) {
            if (const auto* sub = dynamic_cast<const group*>(child.module.get())) {
                collect(*sub, path.empty() ? child.name : path + "." + child.name, out);
            }
        }
    }

    module_registry* registry_;
    std::unique_ptr<pipeline> pipe_;
    std::unique_ptr<pipeline_runner> runner_;
};

}  // namespace atp::studio

#endif  // ATP_STUDIO_SESSION_HPP
