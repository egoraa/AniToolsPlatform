// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_MODULE_FACTORY_BASE_HPP
#define ANITOOLSPLATFORM_HOSTING_MODULE_FACTORY_BASE_HPP

#include <memory>
#include <string_view>

#include <atp/hosting/module_declaration.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/module_config.hpp>
#include <atp/support/version.hpp>

namespace atp {

/// Type-erased module factory, the peer of module_base. Owned by module_registry; the name is
/// fixed at registration time (one type may be aliased under several names), so it is stored in the
/// factory rather than derived from the module type.
class module_factory_base {
   public:
    virtual ~module_factory_base() = default;

    module_factory_base(const module_factory_base&) = delete;
    module_factory_base& operator=(const module_factory_base&) = delete;

    /// Name this factory was registered under.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// Version of the modules it creates.
    [[nodiscard]] virtual version get_version() const noexcept = 0;

    /// The declared config of this factory's modules: structure and defaults, no values. Empty when
    /// the module declares none, which is how "edit this as text" is told from "takes no settings".
    ///
    /// Not pure virtual: a factory written by hand outside the tree declares no config by staying
    /// silent, and that is the honest default.
    ///
    /// The object is built here and filled elsewhere, because filling it means reading a document and
    /// a factory knows nothing about one. What comes back is the module's own config type, so whoever
    /// fills it works through module_config::entry and never has to name that type.
    [[nodiscard]] virtual config_ptr make_config() const {
        return {};
    }

    /// Creates a module, handing it the config it will own.
    ///
    /// The config arrives here, and not in initialize, because the constructor is the only point
    /// earlier than both connect and initialize, and it is where the make<>() calls that declare the
    /// ports happen — so a module could one day derive its ports from the config with no further
    /// change to the platform. Scalar per-instance settings do not belong here: those are module
    /// properties, which the host applies on top of the finished object once it is built. What this
    /// channel is for is a setting a property cannot express — a list, a table, a nested object —
    /// needed before initialize and not edited live.
    ///
    /// The object comes from this very factory's make_config(), filled and checked by the host in
    /// between. Passing one from another factory is a programming error and is refused rather than
    /// cast blindly.
    ///
    /// No default argument is given, deliberately: on a virtual function a default is taken from the
    /// static type of the call, which is a classic trap. The convenient overload that makes the config
    /// itself lives on module_registry instead.
    ///
    /// module_ptr is safe across a plugin boundary — the module's destructor is code of the library
    /// that created it, and the pin in the deleter keeps that library loaded while the module
    /// lives.
    /// @param config config for this instance; empty when the module declares none
    [[nodiscard]] virtual module_ptr create(config_ptr config) const = 0;

    /// Ports and properties the modules of this factory declare — without creating one.
    ///
    /// Pure virtual rather than a default implemented by probing: a factory that cannot describe
    /// itself statically has to say so by writing the fallback, and a plugin registering its own
    /// factory has to answer the question at all. A silent default would have made "the palette
    /// constructs your module" a hidden cost nobody opted into.
    ///
    /// What it does not answer is the config schema; that arrives with the declarable config. Nor
    /// does it take a config: a module whose **set** of ports depends on one cannot be described
    /// statically, which is named honestly rather than papered over, and module_declaration being its
    /// own type is what leaves the door open for declaration(const module_config&).
    /// @throws whatever a probed constructor throws, for a module that names no port node
    [[nodiscard]] virtual module_declaration declaration() const = 0;

   protected:
    module_factory_base() = default;
};

}  // namespace atp

#endif