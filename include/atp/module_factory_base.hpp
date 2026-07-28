#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP

#include <memory>
#include <string_view>

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

    /// Creates a module instance. Takes no parameters: per-instance settings are module properties,
    /// applied on top of the finished object (see runtime::apply_properties).
    ///
    /// module_ptr is safe across a plugin boundary — the module's destructor is code of the library
    /// that created it, and the pin in the deleter keeps that library loaded while the module
    /// lives.
    [[nodiscard]] virtual module_ptr create() const = 0;

   protected:
    module_factory_base() = default;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP