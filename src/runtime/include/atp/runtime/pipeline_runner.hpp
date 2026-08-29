// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_PIPELINE_RUNNER_HPP
#define ATP_RUNTIME_PIPELINE_RUNNER_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/runtime/group.hpp>
#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/thread_name.hpp>

namespace atp::runtime {

/// Pacing mode of a thread: on_demand sleeps when idle (driven by the busy/idle answers of
/// iterate), throttled ticks at a fixed period, spinning never sleeps (latency-critical work).
enum class thread_mode { on_demand, throttled, spinning };

/// Configuration of a runner thread.
struct thread_options {
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};
};

/// Pipeline executor: named threads plus the lifecycle driven through the composite root. Owns the
/// threads and the configuration only; the pipeline is referenced for the duration of the run.
/// Thread names are diagnostics too — they reach the OS and the validation error messages.
///
/// Every control method (add_thread/assign/start/stop/wait/running/error) is owner-thread-only: the
/// runner must not be driven from the pool threads or from module code. A concurrent stop() during
/// wait() is excluded by contract rather than by synchronisation.
class pipeline_runner {
   public:
    pipeline_runner() = default;

    pipeline_runner(const pipeline_runner&) = delete;
    pipeline_runner& operator=(const pipeline_runner&) = delete;

    ~pipeline_runner() {
        stop();
    }

    /// Idle backoff of an on_demand thread: the first idle pass yields, then the sleep doubles up
    /// to the cap; a busy pass resets it.
    static constexpr auto idle_sleep_initial = std::chrono::milliseconds(1);
    static constexpr auto idle_sleep_cap = std::chrono::milliseconds(10);

    /// Declares a thread. Declaration order matters: an unassigned root runs on the first declared
    /// thread.
    /// @throws std::logic_error while the pipeline is running
    /// @throws std::invalid_argument if the period contradicts the mode
    /// @throws std::runtime_error on a duplicate thread name
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

    /// Assigns a group to a thread — the deployment layout. An unassigned group runs inline in its
    /// nearest assigned ancestor.
    /// @throws std::logic_error while the pipeline is running
    /// @throws std::invalid_argument if the thread was never declared
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

    /// Builds the thread maps, validates the connections, runs the initialize and start cascades
    /// through the root and launches the loops. Any failure leaves the runner in its clean,
    /// not-running state — the launch included, which is why running_ is raised before it: a thread
    /// that was created holds this runner's stop_source, and only stop() knows how to reach it.
    /// Installing the notifiers sits under the same rollback as start() for the same reason: it
    /// allocates, and a throw there would otherwise leave every module started with no one left to
    /// stop it.
    /// @throws std::logic_error if the pipeline is already running
    /// @throws std::invalid_argument if an assigned group is not part of this pipeline
    /// @throws std::runtime_error on a cross-thread connection into an unsafe input; anything a
    ///         module's initialize() or start() throws propagates as well
    void start(pipeline& p) {
        if (running_) {
            throw std::logic_error("pipeline is already running");
        }
        if (threads_config_.empty()) {
            threads_config_.push_back({"main", {}});
        }
        {
            std::scoped_lock lock(error_mutex_);
            error_ = nullptr;
        }
        pipeline_ = &p;
        try {
            groups_.clear();
            collect_groups(p.root(), nullptr);
            build_thread_map();
            map_ports();
            validate_connections();
            apply_detach();
            p.root().initialize(p.context());
            try {
                p.root().start();
                install_notifiers();
            } catch (...) {
                try {
                    p.root().stop();
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
                throw;
            }
        } catch (...) {
            uninstall_notifiers();
            undo_detach();
            reset_state();
            throw;
        }
        running_ = true;
        try {
            launch_threads();
        } catch (...) {
            stop();
            throw;
        }
    }

    /// Requests the stop, joins the threads and runs the stop cascade. Idempotent and never throws:
    /// an error from the cascade is reported through error().
    ///
    /// The noexcept is the contract itself rather than a decoration: the destructor calls this, and
    /// a destructor that throws terminates. It is honest because every step under it either cannot
    /// throw (uninstall_notifiers, undo_detach — see there) or is caught and stored (the cascade).
    /// What remains is std::system_error out of join() or a mutex, which is a state the process does
    /// not come back from anyway.
    void stop() noexcept {
        if (!running_) {
            return;
        }
        stop_source_.request_stop();
        shutdown();
    }

    /// Runs until something breaks: blocks until the first execution error, then shuts down as
    /// usual and rethrows the root cause. A healthy pipeline blocks forever — only the owner could
    /// stop it, and the owner is here. The error is rethrown on an already-stopped pipeline too, so
    /// the "stop() then wait()" order does not swallow the root cause.
    /// @throws the first execution error, if there was one
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

    /// First execution error, or nullptr; kept until the next start().
    [[nodiscard]] std::exception_ptr error() const {
        std::scoped_lock lock(error_mutex_);
        return error_;
    }

    /// Whether the threads are running.
    [[nodiscard]] bool running() const {
        return running_;
    }

    /// Pass counters of one thread.
    struct thread_stats {
        std::string name;
        std::uint64_t passes = 0;
        std::uint64_t busy_passes = 0;
    };

    /// Pacing diagnostics: a snapshot of the pass counters per thread, in declaration order. Empty
    /// before the first start(); monotonic for the life of the runner afterwards, so a reader
    /// interested in one run reports the difference. That is the same rule log_ring::dropped() and
    /// group::metrics() keep, and it is the reason a restart does not zero them: the three
    /// measuring surfaces would otherwise disagree about what "since when" means. Reading while
    /// running is fine — the counters are relaxed atomics, and monitoring needs no frame accuracy.
    [[nodiscard]] std::vector<thread_stats> stats() const {
        std::vector<thread_stats> out;
        out.reserve(counters_.size());
        for (std::size_t i = 0; i < counters_.size(); ++i) {
            out.push_back({threads_config_[i].name, counters_[i]->passes.load(std::memory_order_relaxed),
                           counters_[i]->busy.load(std::memory_order_relaxed)});
        }
        return out;
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

    struct group_node {
        group* parent;
        group* node;
    };

    void collect_groups(group& g, group* parent) {
        groups_.push_back({parent, &g});
        for (const group::child& c : g.children()) {
            if (c.subgroup != nullptr) {
                collect_groups(*c.subgroup, &g);
            }
        }
    }

    void build_thread_map() {
        thread_of_.clear();
        std::size_t matched = 0;
        for (const group_node& n : groups_) {
            std::size_t index = n.parent != nullptr ? thread_of_.at(n.parent) : 0;
            auto it = assigned_.find(n.node);
            if (it != assigned_.end()) {
                index = it->second;
                ++matched;
            }
            thread_of_[n.node] = index;
        }
        if (matched != assigned_.size()) {
            throw std::invalid_argument("assigned group is not part of the pipeline");
        }
    }

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

    void apply_detach() {
        for (const group_node& n : groups_) {
            if (n.parent != nullptr && assigned_.contains(n.node)) {
                n.parent->set_detached(*n.node, true);
                detached_.emplace_back(n.parent, n.node);
            }
        }
    }

    /// Returns every detached subgroup to its parent's iterate.
    ///
    /// It clears flags this runner set itself, so a missing child would mean the tree changed
    /// underneath a running pipeline. That is why the non-throwing form is used and its answer
    /// discarded: this runs inside stop(), which is noexcept because the destructor calls it, and a
    /// guarantee that rests on "the child is surely still there" is a guarantee waiting to be broken
    /// by whoever adds a way to remove one.
    void undo_detach() noexcept {
        for (auto& [parent, sub] : detached_) {
            (void)parent->try_set_detached(*sub, false);
        }
        detached_.clear();
    }

    struct thread_signal final : io::notifier_base {
        std::mutex mutex;
        std::condition_variable_any cv;
        bool signaled = false;

        void notify() noexcept override {
            {
                std::scoped_lock lock(mutex);
                signaled = true;
            }
            cv.notify_one();
        }
    };

    void install_notifiers() {
        signals_.clear();
        signals_.reserve(threads_config_.size());
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            signals_.push_back(std::make_unique<thread_signal>());
        }
        for (const group_node& n : groups_) {
            for (const group::connection& c : n.node->connections()) {
                const std::size_t in_thread = port_thread_.at(c.in);
                if (port_thread_.at(c.out) == in_thread) {
                    continue;
                }
                if (threads_config_[in_thread].options.mode != thread_mode::on_demand) {
                    continue;
                }
                c.in->set_notifier(signals_[in_thread].get());
                notified_inputs_.push_back(c.in);
            }
        }
        install_wake_handles();
    }

    /// Points every module's wake handle at the thread that runs it. This is the only place where
    /// that is known: the module gets its host in initialize, while the layout exists only from
    /// start() to stop().
    ///
    /// Only an on_demand thread is wired. A throttled one waits on the same condition variable by
    /// wait_until, so waking it early would break the very period the mode exists for, and a
    /// spinning one never sleeps; on both, wake() stays the documented no-op. A child that is
    /// itself a group gets the notifier of its own thread rather than its parent's, which is what
    /// makes the handle correct for a subgroup the runner detached.
    void install_wake_handles() {
        for (const group_node& n : groups_) {
            const std::size_t group_thread = thread_of_.at(n.node);
            for (const group::child& c : n.node->children()) {
                const group* sub = c.subgroup;
                const std::size_t thread = sub != nullptr ? thread_of_.at(sub) : group_thread;
                if (threads_config_[thread].options.mode != thread_mode::on_demand) {
                    continue;
                }
                c.host->attach(signals_[thread].get());
                attached_hosts_.push_back(c.host.get());
            }
        }
    }

    void uninstall_notifiers() {
        for (io::input_base* in : notified_inputs_) {
            in->set_notifier(nullptr);
        }
        notified_inputs_.clear();
        for (host_node* host : attached_hosts_) {
            host->attach(nullptr);
        }
        attached_hosts_.clear();
        signals_.clear();
    }

    struct pass_counters {
        std::atomic<std::uint64_t> passes{0};
        std::atomic<std::uint64_t> busy{0};
    };

    void launch_threads() {
        stop_source_ = {};
        while (counters_.size() < threads_config_.size()) {
            counters_.push_back(std::make_unique<pass_counters>());
        }
        std::vector<std::vector<group*>> per_thread(threads_config_.size());
        for (const group_node& n : groups_) {
            if (n.parent == nullptr || assigned_.contains(n.node)) {
                per_thread[thread_of_.at(n.node)].push_back(n.node);
            }
        }
        for (std::size_t i = 0; i < threads_config_.size(); ++i) {
            if (per_thread[i].empty()) {
                continue;
            }
            const thread_config& config = threads_config_[i];
            threads_.emplace_back([this, config, signal = signals_[i].get(), counter = counters_[i].get(),
                                   units = std::move(per_thread[i])] {
                detail::set_current_thread_name(config.name);
                run_loop(units, config.options, *signal, *counter);
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

    void run_loop(const std::vector<group*>& units,
                  const thread_options& options,
                  thread_signal& signal,
                  pass_counters& counter) {
        std::stop_token token = stop_source_.get_token();
        std::chrono::milliseconds delay{};
        try {
            while (!token.stop_requested()) {
                const work_status pass = iterate_units(units, token);
                counter.passes.fetch_add(1, std::memory_order_relaxed);
                if (pass == work_status::busy) {
                    counter.busy.fetch_add(1, std::memory_order_relaxed);
                }
                switch (options.mode) {
                    case thread_mode::spinning:
                        std::this_thread::yield();
                        break;
                    case thread_mode::throttled: {
                        const auto next = std::chrono::steady_clock::now() + options.period;
                        std::unique_lock lock(signal.mutex);
                        signal.cv.wait_until(lock, token, next, [] { return false; });
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
                            std::unique_lock lock(signal.mutex);
                            const bool woken = signal.cv.wait_for(lock, token, delay, [&] { return signal.signaled; });
                            signal.signaled = false;
                            if (woken) {
                                delay = {};
                                break;
                            }
                        }
                        delay = std::min(delay * 2, idle_sleep_cap);
                        break;
                }
            }
        } catch (...) {
            capture_error(std::current_exception());
        }
    }

    void capture_error(const std::exception_ptr& e) {
        {
            std::scoped_lock lock(error_mutex_);
            if (!error_) {
                error_ = e;
            }
            stop_source_.request_stop();
        }
        error_cv_.notify_all();
    }

    void store_error(const std::exception_ptr& e) {
        std::scoped_lock lock(error_mutex_);
        if (!error_) {
            error_ = e;
        }
    }

    void shutdown() {
        for (std::jthread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
        uninstall_notifiers();
        try {
            pipeline_->root().stop();
        } catch (...) {
            store_error(std::current_exception());
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
    std::vector<std::pair<group*, group*>> detached_;
    std::vector<std::jthread> threads_;
    std::vector<std::unique_ptr<thread_signal>> signals_;
    std::vector<io::input_base*> notified_inputs_;
    std::vector<host_node*> attached_hosts_;
    std::vector<std::unique_ptr<pass_counters>> counters_;
    std::stop_source stop_source_;
    pipeline* pipeline_ = nullptr;
    bool running_ = false;

    mutable std::mutex error_mutex_;
    std::condition_variable error_cv_;
    std::exception_ptr error_;
};

}  // namespace atp::runtime

#endif
