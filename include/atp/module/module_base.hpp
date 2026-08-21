// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_MODULE_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_MODULE_BASE_HPP

#include <memory>
#include <stop_token>
#include <string_view>

#include <atp/io/threading.hpp>
#include <atp/module/module_context.hpp>
#include <atp/support/version.hpp>

namespace atp::io {
class inputs;
class outputs;
class properties;
}  // namespace atp::io

namespace atp {

using io::work_status;

/// Type-erased base of a module, the peer of io_base/input_base/output_base.
class module_base {
   public:
    virtual ~module_base() = default;

    /// Wires the module up. Called once; whoever needs the context later stores the reference. Here
    /// the module publishes its own interfaces (context.services.provide), and in start() it looks
    /// peers up — the two phases make module initialisation order irrelevant.
    virtual void initialize(module_context& context) = 0;

    /// Starts the module: peers are looked up here, since by now everyone has published.
    virtual void start() = 0;

    /// One pass of the hot path. Gets no context by design.
    /// @return busy if the module did or will do work, idle if it has nothing to do — this drives
    ///         the runner's pacing
    virtual work_status iterate(std::stop_token stop_token) = 0;

    /// Stops the module and removes its publications. Must be correct after initialize() without
    /// start(): a failed start cascade rolls back by calling stop() on everything already
    /// initialised.
    virtual void stop() = 0;

    /// Module version through a type-erased reference; an heir that declared none answers
    /// default_version. The version itself is declared once, as an NTTP of module (module.hpp).
    /// Named get_version rather than version(), because atp::version is a type — the same trick as
    /// std::vector::get_allocator.
    [[nodiscard]] virtual version get_version() const noexcept {
        return default_version;
    }

    /// Type-erased access to the io registries, without which the connection machinery — working
    /// through unique_ptr<module_base> — would not see the ports. module<> implements these as
    /// covariant overrides, so a concrete module sees its own section types and module authors need
    /// to do nothing.
    ///
    /// Pure virtual rather than three pointers planted by the heir: forgetting to plant one would be
    /// a null dereference at the first type-erased call, while forgetting an override is something
    /// the compiler will not let past. Nothing here is on a hot path — the connection machinery
    /// reads these at setup and the description tools on request, while iterate() reaches ports
    /// through the heir's own reference members instead.
    [[nodiscard]] virtual io::inputs& inputs() = 0;
    [[nodiscard]] virtual const io::inputs& inputs() const = 0;
    [[nodiscard]] virtual io::outputs& outputs() = 0;
    [[nodiscard]] virtual const io::outputs& outputs() const = 0;

    /// A module's third registry: the setting values edited on the fly, which the builder, the CLI
    /// and studio all reach through this type-erased path.
    [[nodiscard]] virtual io::properties& properties() = 0;
    [[nodiscard]] virtual const io::properties& properties() const = 0;

    /// Module name through a type-erased reference, symmetrical to get_version. A module that
    /// declared no name is anonymous and answers an empty view.
    [[nodiscard]] virtual std::string_view get_name() const noexcept {
        return {};
    }
};

/// Module deleter carrying a pin: the shared_ptr holds the plugin library against unloading while
/// the module lives, since its vtable and destructor code sit inside it. For monolithic modules the
/// pin is empty and this is a plain delete. The pin is a member of the deleter rather than a
/// shared_ptr control block, so ownership of the module stays unique.
struct module_deleter {
    std::shared_ptr<void> pin;

    void operator()(module_base* m) const noexcept {
        delete m;
    }
};

/// Owning pointer to a module.
using module_ptr = std::unique_ptr<module_base, module_deleter>;

}  // namespace atp

#endif
