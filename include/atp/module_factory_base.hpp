// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP

#include <memory>
#include <string_view>

#include <atp/config_value.hpp>
#include <atp/module_base.hpp>
#include <atp/version.hpp>

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

    /// Creates a module instance from a structured config.
    ///
    /// The config arrives here, and not in initialize, because the constructor is the only point
    /// earlier than both connect and initialize, and it is where the make<>() calls that declare the
    /// ports happen — so a module could one day derive its ports from the config with no further
    /// change to the platform. Scalar per-instance settings do not belong here: those are module
    /// properties, applied on top of the finished object (see runtime::apply_properties). What this
    /// channel is for is a setting a property cannot express — a list, a table, a nested object —
    /// needed before initialize and not edited live.
    ///
    /// No default argument is given, deliberately: on a virtual function a default is taken from the
    /// static type of the call, which is a classic trap. The convenient overload without a config
    /// lives on module_registry instead.
    ///
    /// module_ptr is safe across a plugin boundary — the module's destructor is code of the library
    /// that created it, and the pin in the deleter keeps that library loaded while the module
    /// lives.
    /// @param cfg config for this instance; the null form when the config named none
    [[nodiscard]] virtual module_ptr create(const config_value& cfg) const = 0;

   protected:
    module_factory_base() = default;
};

}  // namespace atp

#endif