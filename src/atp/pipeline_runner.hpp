#ifndef ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP
#define ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/group.hpp>
#include <atp/pipeline.hpp>
#include <atp/thread_name.hpp>

namespace atp {

// Режим темпа потока: on_demand — сон по простою (busy/idle от iterate),
// throttled — фиксированный период тиков, spinning — без сна (латентно-
// критичное). Именованные потоки — ещё и диагностика: имя уходит в ОС и
// в тексты ошибок валидации.
enum class thread_mode { on_demand, throttled, spinning };

struct thread_options {
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};  // throttled: целевой период итераций
};

// Исполнитель пайплайна: именованные потоки + жизненный цикл через корень
// композита. Владеет только потоками и конфигурацией; пайплайн — по ссылке
// на время работы. Все методы управления (add_thread/assign/start/stop/
// wait/running/error) — только с потока-владельца; из потоков пула и кода
// модулей раннером управлять нельзя. Конкурентный stop() во время wait()
// исключён контрактом, а не синхронизацией.
class pipeline_runner {
   public:
    pipeline_runner() = default;

    pipeline_runner(const pipeline_runner&) = delete;
    pipeline_runner& operator=(const pipeline_runner&) = delete;

    ~pipeline_runner() {
        stop();
    }

    // Темп простоя on_demand: первый простойный пасс — yield, дальше сон с
    // удвоением до потолка; busy сбрасывает. Умолчания — параметры режима.
    static constexpr auto idle_sleep_initial = std::chrono::milliseconds(1);
    static constexpr auto idle_sleep_cap = std::chrono::milliseconds(10);

    // Потоки объявляются до старта; порядок объявления значим: корень без
    // назначения исполняется первым объявленным потоком.
    void add_thread(std::string name, thread_options options = {}) {
        if (running_) {
            throw std::logic_error("cannot add threads while pipeline is running");
        }
        if (options.mode == thread_mode::throttled && options.period <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("throttled thread '" + name + "' requires a positive period");
        }
        if (options.mode != thread_mode::throttled && options.period != std::chrono::milliseconds::zero()) {
            throw std::invalid_argument("thread '" + name + "': period is only for throttled mode");
        }
        if (index_of(name)) {
            throw std::runtime_error("duplicate thread name '" + name + "'");
        }
        threads_config_.push_back({std::move(name), options});
    }

    // Раскладка «группа → поток» — конфигурация развёртывания. Неизвестное
    // имя потока отклоняется сразу: add_thread объявляется раньше assign.
    void assign(const group& g, const std::string& thread_name) {
        if (running_) {
            throw std::logic_error("cannot assign while pipeline is running");
        }
        auto index = index_of(thread_name);
        if (!index) {
            throw std::invalid_argument("unknown thread '" + thread_name + "'");
        }
        assigned_[&g] = *index;
    }

    void start(pipeline& p) {
        if (running_) {
            throw std::logic_error("pipeline is already running");
        }
        if (threads_config_.empty()) {
            threads_config_.push_back({"main", {}});  // пустая конфигурация = однопоточный пайплайн
        }
        {
            std::lock_guard lock(error_mutex_);
            error_ = nullptr;
        }
        pipeline_ = &p;
        try {
            // Шаг 1: снимок дерева и чистые проверки конфигурации — до каскадов.
            groups_.clear();
            collect_groups(p.root(), nullptr);
            build_thread_map();
            map_ports();
            validate_connections();
            apply_detach();
            // initialize при сбое откатывается сам (локальный fail-fast групп);
            // сбой start требует внешнего stop: прошедшие initialize без start —
            // контракт module_base.
            p.root().initialize(p.context());
            try {
                p.root().start();
            } catch (...) {
                try {
                    p.root().stop();
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
                throw;
            }
        } catch (...) {
            // Единый откат для любого сбоя: висячий pipeline_ и полузаполненные
            // карты не переживают неудачный start — инвариант «не running —
            // состояние чистое». undo_detach на пустом списке — no-op.
            undo_detach();
            reset_state();
            throw;
        }
        launch_threads();
        running_ = true;
    }

    // Идемпотентен и не бросает: ошибки stop-каскада — через error() (Task 10).
    void stop() {
        if (!running_) {
            return;
        }
        stop_source_.request_stop();
        shutdown();
    }

    // Работать до аварии: блокируется до первой ошибки исполнения, затем
    // обычная остановка и переброс первопричины. У здорового пайплайна
    // блокируется бессрочно — остановить его может только владелец, а он
    // здесь. Предикат сразу общий (ошибка ИЛИ запрошен стоп): будущая
    // остановка по инициативе модулей добавит request_stop+notify под тем
    // же замком, wait() не изменится. Переброс — и на уже остановленном
    // пайплайне: порядок «stop(), потом wait()» не глотает первопричину.
    void wait() {
        if (running_) {
            std::stop_token token = stop_source_.get_token();
            {
                std::unique_lock lock(error_mutex_);
                error_cv_.wait(lock, [&] { return error_ != nullptr || token.stop_requested(); });
            }
            shutdown();
        }
        if (std::exception_ptr e = error()) {
            std::rethrow_exception(e);
        }
    }

    [[nodiscard]] std::exception_ptr error() const {
        std::lock_guard lock(error_mutex_);
        return error_;
    }

    [[nodiscard]] bool running() const {
        return running_;
    }

   private:
    struct thread_config {
        std::string name;
        thread_options options;
    };

    [[nodiscard]] std::optional<std::size_t> index_of(const std::string& name) const {
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            if (threads_config_[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    // Снимок дерева групп на время start(): DFS pre-order, корень первым
    // (parent == nullptr). Единственное место распознавания подгруппы
    // (dynamic_cast); все фазы старта — плоские циклы по снимку.
    struct group_node {
        group* parent;
        group* node;
    };

    void collect_groups(group& g, group* parent) {
        groups_.push_back({parent, &g});
        for (const group::child& c : g.children()) {
            if (auto* sub = dynamic_cast<group*>(c.module.get())) {
                collect_groups(*sub, &g);
            }
        }
    }

    void build_thread_map() {
        thread_of_.clear();
        std::size_t matched = 0;
        for (const group_node& n : groups_) {
            // pre-order: родитель уже в карте — наследование потока тривиально
            std::size_t index = n.parent != nullptr ? thread_of_.at(n.parent) : 0;
            auto it = assigned_.find(n.node);
            if (it != assigned_.end()) {
                index = it->second;
                ++matched;
            }
            thread_of_[n.node] = index;
        }
        // Каждое назначение обязано найтись в дереве: молча проигнорированная
        // «чужая» группа — незамеченная ошибка конфигурации.
        if (matched != assigned_.size()) {
            throw std::invalid_argument("assigned group is not part of the pipeline");
        }
    }

    // Карта «порт → поток»: владеемые порты каждого модуля получают поток
    // его исполняющей группы. Ребёнку-подгруппе отдельная ветка не нужна:
    // реестры групп держат одни алиасы, owned() у них пуст.
    void map_ports() {
        port_thread_.clear();
        for (const group_node& n : groups_) {
            const std::size_t index = thread_of_.at(n.node);
            for (const group::child& c : n.node->children()) {
                for (io::input_base* port : c.module->inputs().owned()) {
                    port_thread_[port] = index;
                }
                for (io::output_base* port : c.module->outputs().owned()) {
                    port_thread_[port] = index;
                }
            }
        }
    }

    // Критерий — граница потоков, не групп: соседям по потоку safe не нужен.
    void validate_connections() const {
        for (const group_node& n : groups_) {
            for (const group::connection& c : n.node->connections()) {
                const std::size_t out_thread = port_thread_.at(c.out);
                const std::size_t in_thread = port_thread_.at(c.in);
                if (out_thread != in_thread && !c.in->thread_safe()) {
                    throw std::runtime_error("cross-thread connection into unsafe input '" + c.in->name() +
                                             "' between threads '" + threads_config_[out_thread].name + "' and '" +
                                             threads_config_[in_thread].name + "'");
                }
            }
        }
    }

    // Назначенные подгруппы исполняются своими потоками — из iterate
    // родителей они исключаются на время работы.
    void apply_detach() {
        for (const group_node& n : groups_) {
            if (n.parent != nullptr && assigned_.contains(n.node)) {
                n.parent->set_detached(*n.node, true);
                detached_.push_back({n.parent, n.node});
            }
        }
    }

    void undo_detach() {
        for (auto& [parent, sub] : detached_) {
            parent->set_detached(*sub, false);
        }
        detached_.clear();
    }

    void launch_threads() {
        stop_source_ = {};  // свежий источник: раннер мог уже отработать цикл
        std::vector<std::vector<group*>> per_thread(threads_config_.size());
        // Единицы исполнения: корень (умолчание — первый объявленный поток) +
        // явно назначенные группы, в DFS-порядке снимка.
        for (const group_node& n : groups_) {
            if (n.parent == nullptr || assigned_.contains(n.node)) {
                per_thread[thread_of_.at(n.node)].push_back(n.node);
            }
        }
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            if (per_thread[i].empty()) {
                continue;  // пустому потоку нечего делать — не создаём
            }
            const thread_config& config = threads_config_[i];
            threads_.emplace_back([this, config, units = std::move(per_thread[i])] {
                detail::set_current_thread_name(config.name);
                run_loop(units, config.options);
            });
        }
    }

    [[nodiscard]] work_status iterate_units(const std::vector<group*>& units, const std::stop_token& token) {
        work_status pass = work_status::idle;
        for (group* g : units) {
            if (g->iterate(token) == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

    void run_loop(const std::vector<group*>& units, const thread_options& options) {
        std::stop_token token = stop_source_.get_token();
        // Сон прерываем стоп-токеном: request_stop (ошибка или stop()) будит
        // мгновенно, спящий поток не оттягивает остановку.
        std::mutex sleep_mutex;
        std::condition_variable_any sleep_cv;
        std::chrono::milliseconds delay{};  // on_demand: 0 — ещё не спим, только yield
        try {
            while (!token.stop_requested()) {
                const work_status pass = iterate_units(units, token);
                switch (options.mode) {
                    case thread_mode::spinning:
                        std::this_thread::yield();
                        break;
                    case thread_mode::throttled: {
                        // скольжение: пропущенные тики не навёрстываем — темп ровный
                        const auto next = std::chrono::steady_clock::now() + options.period;
                        std::unique_lock lock(sleep_mutex);
                        sleep_cv.wait_until(lock, token, next, [] { return false; });
                        break;
                    }
                    case thread_mode::on_demand:
                        if (pass == work_status::busy) {
                            delay = {};
                            break;
                        }
                        if (delay == std::chrono::milliseconds{}) {
                            std::this_thread::yield();
                            delay = idle_sleep_initial;
                            break;
                        }
                        {
                            std::unique_lock lock(sleep_mutex);
                            sleep_cv.wait_for(lock, token, delay, [] { return false; });
                        }
                        delay = std::min(delay * 2, idle_sleep_cap);
                        break;
                }
            }
        } catch (...) {
            capture_error(std::current_exception());
        }
    }

    // Первая ошибка побеждает; остановка — общая на весь пайплайн.
    // request_stop под замком: он входит в предикат wait() (Task 10),
    // изменение условия вне мьютекса — потерянное пробуждение.
    void capture_error(std::exception_ptr e) {
        {
            std::lock_guard lock(error_mutex_);
            if (!error_) {
                error_ = std::move(e);
            }
            stop_source_.request_stop();
        }
        error_cv_.notify_all();
    }

    void store_error(std::exception_ptr e) {
        std::lock_guard lock(error_mutex_);
        if (!error_) {
            error_ = std::move(e);
        }
    }

    // Общий хвост stop()/wait(): дождаться потоков, stop-каскад через корень,
    // сбросить рабочее состояние.
    void shutdown() {
        for (std::jthread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
        try {
            pipeline_->root().stop();
        } catch (...) {
            store_error(std::current_exception());  // stop() не бросает — ошибка доступна через error()
        }
        undo_detach();
        reset_state();
    }

    void reset_state() {
        groups_.clear();
        thread_of_.clear();
        port_thread_.clear();
        pipeline_ = nullptr;
        running_ = false;
    }

    std::vector<thread_config> threads_config_;
    std::unordered_map<const group*, std::size_t> assigned_;
    std::unordered_map<const group*, std::size_t> thread_of_;
    std::vector<group_node> groups_;
    std::unordered_map<const io::io_base*, std::size_t> port_thread_;
    std::vector<std::pair<group*, group*>> detached_;  // (родитель, подгруппа) — для отката
    std::vector<std::jthread> threads_;
    std::stop_source stop_source_;
    pipeline* pipeline_ = nullptr;
    bool running_ = false;

    mutable std::mutex error_mutex_;
    std::condition_variable error_cv_;  // будит wait(); условия предиката меняются под error_mutex_
    std::exception_ptr error_;          // первая ошибка исполнения; хранится до следующего start()
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_PIPELINE_RUNNER_HPP
