// SPDX-License-Identifier: Apache-2.0
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

struct probe_event {
    std::string module;
    std::string phase;
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

    [[nodiscard]] std::vector<std::string> order_of(const std::string& phase) const {
        std::vector<std::string> result;
        for (const probe_event& e : snapshot()) {
            if (e.phase == phase) {
                result.push_back(e.module);
            }
        }
        return result;
    }

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

class probe_module : public atp::module<> {
   public:
    probe_module(event_log& log, std::string name) : log_(&log), name_(std::move(name)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    std::latch* first_iterate = nullptr;
    std::string throw_in;

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
        return atp::work_status::busy;
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
    bool signaled_ = false;
};

}  // namespace atp_tests

#endif
