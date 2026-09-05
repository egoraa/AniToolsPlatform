// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_GROUP_HPP
#define ATP_RUNTIME_GROUP_HPP

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/hosting/module_registry.hpp>
#include <atp/io.hpp>
#include <atp/module/module_base.hpp>
#include <atp/runtime/host_node.hpp>

namespace atp::runtime {

/// Composite module: owns its children (modules and subgroups — subgroups being modules too), runs
/// the lifecycle as cascades over them and publishes selected child ports as aliases in its own
/// registries (see expose_*).
///
/// Not a unit of execution: how a group runs — on a thread of its own or inline in an ancestor — is
/// decided by the runner's layout, and nesting here is encapsulation only. Not thread-safe:
/// composition and exporting belong to the setup phase. The cascades are called by the runner, so a
/// hand-written root.initialize() bypassing it means running them twice.
class group : public module_base {
   public:
    /// Live counters of one child, written by the thread running the cascade and read by whoever is
    /// monitoring. Relaxed atomics for the same reason pipeline_runner's pass counters are: a
    /// monitor needs a number that is current, not one that is frame-accurate, and making the hot
    /// path pay for ordering nobody reads would be measuring the measurement.
    ///
    /// Held through a pointer because `child` lives in a vector, and an atomic is neither copyable
    /// nor movable — growing the vector would not compile.
    struct child_counters {
        std::atomic<std::uint64_t> calls{0};
        std::atomic<std::uint64_t> busy{0};
        std::atomic<std::uint64_t> total_ns{0};
        std::atomic<std::uint64_t> max_ns{0};

        void record(std::chrono::nanoseconds spent, bool was_busy) noexcept {
            const auto ns = static_cast<std::uint64_t>(spent.count() < 0 ? 0 : spent.count());
            calls.fetch_add(1, std::memory_order_relaxed);
            if (was_busy) {
                busy.fetch_add(1, std::memory_order_relaxed);
            }
            total_ns.fetch_add(ns, std::memory_order_relaxed);
            std::uint64_t seen = max_ns.load(std::memory_order_relaxed);
            while (ns > seen && !max_ns.compare_exchange_weak(seen, ns, std::memory_order_relaxed)) {
            }
        }
    };

    /// A child entry: the name within the group's scope plus ownership (module_ptr carries the DLL
    /// pin, so a plugin module holds its own library).
    struct child {
        std::string name;
        module_ptr module;
        /// The child runs on a thread of its own and the parent's iterate skips it; set and cleared
        /// by the runner.
        bool detached = false;
        std::unique_ptr<child_counters> counters = std::make_unique<child_counters>();
        /// The platform's side of this child: its log buffer and its wake handle. Created with the
        /// child and destroyed with it, which is why a module may keep the reference it was given
        /// in initialize for as long as it lives.
        std::unique_ptr<host_node> host = std::make_unique<host_node>();
        /// The child seen as a composite, or nullptr for an ordinary module. Recorded once by add(),
        /// the single funnel every child goes through, rather than recovered by dynamic_cast at each
        /// of the places that walk the tree: whoever puts a subgroup here knows statically that it is
        /// one, and the field keeps that knowledge instead of throwing it away and guessing it back.
        group* subgroup = nullptr;
    };

    /// What one module cost, as the composite that ran it observed.
    ///
    /// For a subgroup the time is its whole subtree, because that is what the parent's pass actually
    /// spent — minus any child the runner detached onto a thread of its own, which the cascade skips
    /// and therefore never charges here. A reader comparing a group against the sum of its children
    /// sees the group's own overhead, which is the honest decomposition.
    struct module_stats {
        /// Dotted path from the group that was asked; a direct child is a bare name.
        std::string path;
        std::uint64_t calls = 0;
        std::uint64_t busy_calls = 0;
        std::chrono::nanoseconds total{};
        std::chrono::nanoseconds max{};
    };

    /// Counters of one input, addressed the way the runtime addresses everything else.
    ///
    /// The path lives here rather than in io::input_stats because a dotted path is a runtime
    /// notion: an input knows what it received, not that it sits inside a tree, and teaching the
    /// SDK about trees to save a wrapper would be the wrong trade.
    struct port_stats {
        /// Dotted path from the group that was asked, as "module.port".
        std::string path;
        io::input_stats stats;
    };

    /// @param name group name, used for diagnostics
    explicit group(std::string name) : name_(std::move(name)) {}

    group(const group&) = delete;
    group& operator=(const group&) = delete;

    /// Name of the group itself; the names of children live in the parent's entries.
    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    /// Takes over a ready-made module, module_registry::create() results included.
    ///
    /// This is the one place a child enters a group — make() and add_group() both come through here —
    /// so it is also the one place that asks whether the child is a composite, recording the answer
    /// in the entry. The cast stays here rather than moving into make() as a static test, because
    /// this overload takes module_registry::create() results and a group can arrive through it: a
    /// static test would leave the invariant resting on "nobody adds a group this way".
    /// @throws std::invalid_argument on a null module or an empty name
    /// @throws std::runtime_error if the name is already taken in this group
    module_base& add(std::string name, module_ptr module) {
        if (!module) {
            throw std::invalid_argument("null module for '" + name + "' in group '" + name_ + "'");
        }
        ensure_unique(name);
        children_.push_back({std::move(name), std::move(module), false, std::make_unique<child_counters>(),
                             std::make_unique<host_node>()});
        child& entry = children_.back();
        entry.subgroup = dynamic_cast<group*>(entry.module.get());
        return *entry.module;
    }  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

    /// Creates a child module in place.
    /// @param name name within this group's scope
    /// @param args constructor arguments of the module
    /// @throws std::runtime_error if the name is already taken in this group
    template <std::derived_from<module_base> TModule, typename... TArgs>
        requires std::constructible_from<TModule, TArgs...>
    TModule& make(std::string name, TArgs&&... args) {
        module_ptr module(new TModule(std::forward<TArgs>(args)...), {});
        TModule& ref = static_cast<TModule&>(*module);
        add(std::move(name), std::move(module));
        return ref;
    }

    /// Creates a child module under the name it declares itself (contract has_module_name). Spell
    /// the name out when the overloads collide.
    /// @throws std::runtime_error if the name is already taken in this group
    template <std::derived_from<module_base> TModule, typename... TArgs>
        requires has_module_name<TModule> && std::constructible_from<TModule, TArgs...>
    TModule& make(TArgs&&... args) {
        return make<TModule>(std::string{TModule::module_name}, std::forward<TArgs>(args)...);
    }

    /// Creates a subgroup, which is just another child module; the name is duplicated into its
    /// constructor for get_name().
    /// @throws std::runtime_error if the name is already taken in this group
    group& add_group(std::string name) {
        std::string ctor_name = name;
        return make<group>(std::move(name), std::move(ctor_name));
    }

    /// Child entries in insertion order.
    [[nodiscard]] const std::vector<child>& children() const {
        return children_;
    }

    /// Child module by name; nullptr if there is none.
    [[nodiscard]] module_base* find_module(const std::string& name) const {
        const child* c = find_child(name);
        return c ? c->module.get() : nullptr;
    }

    /// Child subgroup by name; nullptr if there is none or the child is an ordinary module.
    [[nodiscard]] group* find_group(const std::string& name) const {
        const child* c = find_child(name);
        return c != nullptr ? c->subgroup : nullptr;
    }

    /// Service hook for the runner: a subgroup assigned to its own thread is excluded from the
    /// parent's iterate. Not to be called outside the runner.
    /// @throws std::invalid_argument if the group has no such child
    void set_detached(const group& detached_child, bool value) {
        if (!try_set_detached(detached_child, value)) {
            throw std::invalid_argument("group '" + name_ + "' has no such child group");
        }
    }

    /// Non-throwing form of set_detached, for the runner's undo path.
    ///
    /// It exists so that undoing a detach cannot throw at all rather than merely not throwing in
    /// practice: the undo runs inside pipeline_runner::stop(), which is noexcept because a
    /// destructor calls it. Reasoning that the child is still there — a group is add-only, so it is —
    /// would leave the guarantee resting on an invariant a future removal API could break silently.
    /// @return false if the group has no such child
    [[nodiscard]] bool try_set_detached(const group& detached_child, bool value) noexcept {
        for (child& c : children_) {
            if (c.module.get() == &detached_child) {
                c.detached = value;
                return true;
            }
        }
        return false;
    }

    /// Publishes a child input in this group's own registry, under a new name.
    ///
    /// The path form is "<child>.<port>" only: the port is found through the child's module
    /// interface, so a module and a subgroup are indistinguishable and a re-export resolves
    /// straight to the real port. There is deliberately no overload taking a reference — a port's
    /// membership in the group cannot be verified, and a caller's mistake would silently distort
    /// the cross-thread validation.
    /// @throws std::invalid_argument on a malformed path
    /// @throws std::runtime_error if the child or the port is missing, or the alias name is taken
    void expose_input(std::string alias, const std::string& path) {
        inputs_.alias(std::move(alias), resolve_input(path));
    }

    /// Publishes a child output in this group's own registry, under a new name. Same path rules as
    /// expose_input().
    /// @throws std::invalid_argument on a malformed path
    /// @throws std::runtime_error if the child or the port is missing, or the alias name is taken
    void expose_output(std::string alias, const std::string& path) {
        outputs_.alias(std::move(alias), resolve_output(path));
    }

    /// A recorded connection — a pair of ports and nothing more: there are no owners here, and the
    /// runner builds the port-to-thread map for validation from the modules' owned ports itself.
    struct connection {
        io::output_base* out;
        io::input_base* in;
    };

    /// Connects two ports by paths within this group's scope.
    /// @throws std::invalid_argument on a malformed path
    /// @throws std::runtime_error if a child or a port is missing, or the ports are incompatible
    void connect(const std::string& from, const std::string& to) {
        link(from, to);
    }

    /// Connections recorded by this group, in creation order.
    [[nodiscard]] const std::vector<connection>& connections() const {
        return connections_;
    }

    ~group() override {
        for (const connection& c : connections_) {
            (void)c.out->disconnect(*c.in);
        }
    }

    /// A group's ports are aliases of its children's ports, filled in by expose_*.
    [[nodiscard]] io::inputs& inputs() override {
        return inputs_;
    }
    [[nodiscard]] const io::inputs& inputs() const override {
        return inputs_;
    }
    [[nodiscard]] io::outputs& outputs() override {
        return outputs_;
    }
    [[nodiscard]] const io::outputs& outputs() const override {
        return outputs_;
    }

    /// A composite group has no properties of its own, so the registry stays empty: child
    /// properties are reached by path (see runtime::property_override), and group-level aliases do
    /// not exist until there is a need for them.
    [[nodiscard]] io::properties& properties() override {
        return properties_;
    }
    [[nodiscard]] const io::properties& properties() const override {
        return properties_;
    }

    /// Initialises the children in insertion order, fail-fast: a throwing child means stop() for
    /// everything already initialised, in reverse order (rollback errors are swallowed — the root
    /// cause matters more), and then a rethrow. Outer groups roll their own earlier children back
    /// by the same logic, recursively.
    ///
    /// Each child is handed a context of its own, differing from the one received in a single
    /// field: the services are shared by the whole pipeline, the host is the child's. That is what
    /// lets a log line name its author although the author never says who it is.
    void initialize(module_context& context) override {
        std::size_t done = 0;
        try {
            for (child& c : children_) {
                module_context child_context{context.services, *c.host};
                c.module->initialize(child_context);
                ++done;
            }
        } catch (...) {
            for (std::size_t i = done; i > 0; --i) {
                try {
                    children_[i - 1].module->stop();
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
            }
            throw;
        }
    }

    /// Starts the children in insertion order. There is no rollback here: on an error the runner
    /// calls root.stop(), and stop() is required to be correct after initialize without start (the
    /// module_base contract).
    void start() override {
        for (child& c : children_) {
            c.module->start();
        }
    }

    /// Stops the children in reverse order, carrying on past a failing one and rethrowing the first
    /// error at the end: stopping matters more than diagnosing.
    void stop() override {
        std::exception_ptr first;
        for (child& c : std::views::reverse(children_)) {
            try {
                c.module->stop();
            } catch (...) {
                if (!first) {
                    first = std::current_exception();
                }
            }
        }
        if (first) {
            std::rethrow_exception(first);
        }
    }

    /// Iterates the children in insertion order, skipping the detached ones — they have threads of
    /// their own.
    /// @return busy if at least one child was busy
    work_status iterate(std::stop_token token) override {
        work_status pass = work_status::idle;
        for (child& c : children_) {
            if (token.stop_requested()) {
                return pass;
            }
            if (c.detached) {
                continue;
            }
            work_status status = work_status::idle;
            if (metrics_enabled_.load(std::memory_order_relaxed)) {
                const auto begin = std::chrono::steady_clock::now();
                status = c.module->iterate(token);
                c.counters->record(std::chrono::steady_clock::now() - begin, status == work_status::busy);
            } else {
                status = c.module->iterate(token);
            }
            if (status == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

    /// Turns the per-module timing on or off for this group and, by default, everything under it.
    ///
    /// Off by default, and that is a measured decision rather than caution: timing every child's
    /// iterate costs a pair of steady_clock::now() calls per pass, which took a two-module pipeline
    /// from 4.49 to 3.35 M items/s — a quarter of the throughput, because a trivial iterate is
    /// cheaper than reading the clock twice. Diagnosing a slow pipeline is worth that; running one
    /// is not. With it off the hot path pays one relaxed load and a branch the predictor gets right.
    void set_metrics_enabled(bool on, bool recursive = true) noexcept {
        metrics_enabled_.store(on, std::memory_order_relaxed);
        if (!recursive) {
            return;
        }
        for (const child& c : children_) {
            if (c.subgroup != nullptr) {
                c.subgroup->set_metrics_enabled(on, true);
            }
        }
    }

    /// Whether this group is timing its children.
    [[nodiscard]] bool metrics_enabled() const noexcept {
        return metrics_enabled_.load(std::memory_order_relaxed);
    }

    /// Per-module cost of everything under this group, depth first, in cascade order.
    ///
    /// This is the answer to "which module is costing the time", which the per-thread pass counters
    /// of pipeline_runner cannot give: a thread runs an ordered list of groups and one slow iterate
    /// among twenty looks exactly like twenty slightly slow ones. Reading while running is fine.
    /// The counters are monotonic for the life of the group, like the runner's own.
    [[nodiscard]] std::vector<module_stats> metrics() const {
        std::vector<module_stats> out;
        collect_metrics(std::string(), out);
        return out;
    }

    /// What every input under this group received and lost, depth first, addressed as "module.port".
    ///
    /// Kept apart from module_stats rather than folded into it: the key is a port and not a module,
    /// and unlike the timing these counters are never switched off — they cost an increment under a
    /// lock the writer already holds, so there is nothing to save by hiding them and a switch would
    /// only give an operator a way to be blind to data loss.
    [[nodiscard]] std::vector<port_stats> input_metrics() const {
        std::vector<port_stats> out;
        collect_input_metrics(std::string(), out);
        return out;
    }

    /// Drains the log buffers of the subtree, composing each module's dotted path as it walks.
    ///
    /// It empties what it reads, so exactly one caller may do this — the host draining into its
    /// sink.
    /// @param prefix path of this group, empty for the root
    /// @param out lines are appended, oldest first within one module
    void collect_logs(const std::string& prefix, std::vector<log_line>& out) {
        for (child& c : children_) {
            const std::string path = prefix.empty() ? c.name : prefix + "." + c.name;
            c.host->ring().drain([&out, &path](log_level level, std::string_view text, bool truncated,
                                               std::chrono::system_clock::time_point at) {
                out.push_back({path, level, std::string(text), truncated, at});
            });
            if (c.subgroup != nullptr) {
                c.subgroup->collect_logs(path, out);
            }
        }
    }

   private:
    /// Depth-first walk building the dotted paths.
    void collect_metrics(const std::string& prefix, std::vector<module_stats>& out) const {
        for (const child& c : children_) {
            const std::string path = prefix.empty() ? c.name : prefix + "." + c.name;
            out.push_back({path, c.counters->calls.load(std::memory_order_relaxed),
                           c.counters->busy.load(std::memory_order_relaxed),
                           std::chrono::nanoseconds(c.counters->total_ns.load(std::memory_order_relaxed)),
                           std::chrono::nanoseconds(c.counters->max_ns.load(std::memory_order_relaxed))});
            if (const group* nested = c.subgroup) {
                nested->collect_metrics(path, out);
            }
        }
    }

    /// Depth-first walk of the ports, skipping a group's own registry on the way down: what a group
    /// declares are aliases to the children's ports, and reporting both would count one input twice
    /// under two names.
    void collect_input_metrics(const std::string& prefix, std::vector<port_stats>& out) const {
        for (const child& c : children_) {
            const std::string path = prefix.empty() ? c.name : prefix + "." + c.name;
            if (const group* nested = c.subgroup) {
                nested->collect_input_metrics(path, out);
                continue;
            }
            for (const auto& [port_name, in] : c.module->inputs().entries()) {
                out.push_back({path + "." + port_name, in->stats()});
            }
        }
    }

    [[nodiscard]] const child* find_child(const std::string& name) const {
        for (const child& c : children_) {
            if (c.name == name) {
                return &c;
            }
        }
        return nullptr;
    }

    void ensure_unique(const std::string& name) const {
        if (name.empty()) {
            throw std::invalid_argument("empty child name in group '" + name_ + "'");
        }
        if (find_child(name)) {
            throw std::runtime_error("duplicate name '" + name + "' in group '" + name_ + "'");
        }
    }

    [[nodiscard]] std::pair<module_base*, std::string> split_path(const std::string& path) const {
        auto dot = path.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == path.size()) {
            throw std::invalid_argument("path '" + path + "' in group '" + name_ + "': expected '<child>.<port>'");
        }
        module_base* child_module = find_module(path.substr(0, dot));
        if (!child_module) {
            throw std::runtime_error("group '" + name_ + "' has no child '" + path.substr(0, dot) + "'");
        }
        return {child_module, path.substr(dot + 1)};
    }

    [[nodiscard]] io::input_base& resolve_input(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::input_base* port = child_module->inputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no input '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }

    [[nodiscard]] io::output_base& resolve_output(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::output_base* port = child_module->outputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no output '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }

    void link(const std::string& from, const std::string& to) {
        io::output_base& out = resolve_output(from);
        io::input_base& in = resolve_input(to);
        out.connect(in);
        connections_.push_back({&out, &in});
    }

    std::string name_;
    std::atomic<bool> metrics_enabled_{false};
    std::vector<child> children_;
    std::vector<connection> connections_;
    io::inputs inputs_;
    io::outputs outputs_;
    io::properties properties_;
};

}  // namespace atp::runtime

#endif
