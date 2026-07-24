#ifndef ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP
#define ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP

#include <latch>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <atp/module.hpp>

namespace atp_tests {

// Журнал событий жизненного цикла — общий для теста и модулей-зондов.
// Потокобезопасен: iterate пишут потоки пула.
struct probe_event {
    std::string module;
    std::string phase;  // "initialize" / "start" / "iterate" / "stop"
    std::thread::id thread;
};

class event_log {
   public:
    void record(const std::string& module, const std::string& phase) {
        std::lock_guard lock(mutex_);
        events_.push_back({module, phase, std::this_thread::get_id()});
    }

    [[nodiscard]] std::vector<probe_event> snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    // Имена модулей в порядке событий заданной фазы — для проверки каскадов.
    [[nodiscard]] std::vector<std::string> order_of(const std::string& phase) const {
        std::vector<std::string> result;
        for (const probe_event& e : snapshot()) {
            if (e.phase == phase) {
                result.push_back(e.module);
            }
        }
        return result;
    }

    // Поток, писавший iterate модуля (первое событие); id{} — не итерировался.
    [[nodiscard]] std::thread::id iterate_thread(const std::string& module) const {
        for (const probe_event& e : snapshot()) {
            if (e.module == module && e.phase == "iterate") {
                return e.thread;
            }
        }
        return {};
    }

   private:
    mutable std::mutex mutex_;
    std::vector<probe_event> events_;
};

// Модуль-зонд: пишет фазы в журнал, по указанию бросает из фазы,
// сигналит latch-ем о первом iterate — тесты ждут без sleep.
class probe_module : public atp::module<> {
   public:
    probe_module(event_log& log, std::string name) : log_(&log), name_(std::move(name)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    std::latch* first_iterate = nullptr;  // не владеет; nullptr — не сигналить
    std::string throw_in;                 // фаза, из которой бросить (после записи в журнал)

    void initialize(atp::module_context&) override {
        hit("initialize");
    }
    void start() override {
        hit("start");
    }
    atp::work_status iterate(std::stop_token) override {
        if (first_iterate && !signaled_) {
            signaled_ = true;
            first_iterate->count_down();
        }
        hit("iterate");
        return atp::work_status::busy;  // журнал пишется каждый вызов — это работа
    }
    void stop() override {
        hit("stop");
    }

   private:
    void hit(const char* phase) {
        log_->record(name_, phase);
        if (throw_in == phase) {
            throw std::runtime_error(name_ + ": failure in " + phase);
        }
    }

    event_log* log_;
    std::string name_;
    bool signaled_ = false;  // latch сигналится один раз; поле читает только поток группы
};

}  // namespace atp_tests

#endif  // ANITOOLSPLATFORM_TESTS_PIPELINE_TEST_SUPPORT_HPP
