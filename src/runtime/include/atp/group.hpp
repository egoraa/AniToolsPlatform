#ifndef ANITOOLSPLATFORM_GROUP_HPP
#define ANITOOLSPLATFORM_GROUP_HPP

#include <concepts>
#include <exception>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/module_registry.hpp>

namespace atp {

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
    /// A child entry: the name within the group's scope plus ownership (module_ptr carries the DLL
    /// pin, so a plugin module holds its own library).
    struct child {
        std::string name;
        module_ptr module;
        /// The child runs on a thread of its own and the parent's iterate skips it; set and cleared
        /// by the runner.
        bool detached = false;
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
    /// @throws std::invalid_argument on a null module or an empty name
    /// @throws std::runtime_error if the name is already taken in this group
    module_base& add(std::string name, module_ptr module) {
        if (!module) {
            throw std::invalid_argument("null module for '" + name + "' in group '" + name_ + "'");
        }
        ensure_unique(name);
        children_.push_back({std::move(name), std::move(module), false});
        return *children_.back().module;
    }

    /// Creates a child module in place.
    /// @param name name within this group's scope
    /// @param args constructor arguments of the module
    /// @throws std::runtime_error if the name is already taken in this group
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, TArgs...>
    M& make(std::string name, TArgs&&... args) {
        module_ptr module(new M(std::forward<TArgs>(args)...), {});
        M& ref = static_cast<M&>(*module);
        add(std::move(name), std::move(module));
        return ref;
    }

    /// Creates a child module under the name it declares itself (contract has_module_name). Spell
    /// the name out when the overloads collide.
    /// @throws std::runtime_error if the name is already taken in this group
    template <std::derived_from<module_base> M, typename... TArgs>
        requires has_module_name<M> && std::constructible_from<M, TArgs...>
    M& make(TArgs&&... args) {
        return make<M>(std::string{M::module_name}, std::forward<TArgs>(args)...);
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
        return dynamic_cast<group*>(find_module(name));
    }

    /// Service hook for the runner: a subgroup assigned to its own thread is excluded from the
    /// parent's iterate. Not to be called outside the runner.
    /// @throws std::invalid_argument if the group has no such child
    void set_detached(const group& detached_child, bool value) {
        for (child& c : children_) {
            if (c.module.get() == &detached_child) {
                c.detached = value;
                return;
            }
        }
        throw std::invalid_argument("group '" + name_ + "' has no such child group");
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
        link(from, to, false);
    }

    /// Connects two ports and immediately delivers the output's cached value, if there is one.
    /// @throws std::invalid_argument on a malformed path
    /// @throws std::runtime_error if a child or a port is missing, or the ports are incompatible
    void connect(const std::string& from, const std::string& to, io::replay_t) {
        link(from, to, true);
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
    void initialize(module_context& context) override {
        std::size_t done = 0;
        try {
            for (child& c : children_) {
                c.module->initialize(context);
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
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            try {
                it->module->stop();
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
            if (c.module->iterate(token) == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

   private:
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

    void link(const std::string& from, const std::string& to, bool deliver_cached) {
        io::output_base& out = resolve_output(from);
        io::input_base& in = resolve_input(to);
        if (deliver_cached) {
            out.connect(in, io::replay);
        } else {
            out.connect(in);
        }
        connections_.push_back({&out, &in});
    }

    std::string name_;
    std::vector<child> children_;
    std::vector<connection> connections_;
    io::inputs inputs_;
    io::outputs outputs_;
    io::properties properties_;
};

}  // namespace atp

#endif
