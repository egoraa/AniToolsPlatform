// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_MODULE_HPP

#include <stop_token>
#include <string_view>
#include <utility>

#include <atp/io.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/ports.hpp>
#include <atp/support/fixed_string.hpp>
#include <atp/support/version.hpp>

namespace atp {

/// Base class of every concrete module: implements module_base and holds the three port sections.
///
/// The sections are named as one parameter, a ports<> list; the name comes second and the version
/// third, because the name is needed more often and C++ has no way to skip a default. An empty name
/// means anonymous: such a module can only be registered under an explicit name
/// (module_registry::add<M>(name)).
/// @tparam TPorts list naming the inputs, outputs and properties section types
/// @tparam Name module name, empty for an anonymous module
/// @tparam Version module version
template <ports_list TPorts = ports<>, detail::fixed_string Name = "", version Version = default_version>
class module : public module_base {
   public:
    static constexpr std::string_view module_name = Name.view();
    static constexpr version module_version = Version;

    /// The port node this module was declared with, so a factory can build one and read the ports off
    /// it without constructing the module. The three section types stay reachable as
    /// ports_type::in_type and its two peers, which is why they are not re-exported one by one.
    using ports_type = TPorts;

    module() = default;

    /// Takes over a pre-wired port node: ports live on the heap, so moving the node breaks neither
    /// the sections' reference members nor the established connections.
    explicit module(TPorts io) : io_(std::move(io)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return module_name;
    }
    [[nodiscard]] version get_version() const noexcept override {
        return Version;
    }

    void initialize(module_context&) override {}
    void start() override {}
    work_status iterate(std::stop_token) override {
        return work_status::idle;
    }
    void stop() override {}

    /// Covariant overrides: a concrete module sees its own section types (inputs().step,
    /// outputs().count, properties().limit), while the machinery reaching it through module_base
    /// sees the same registries type-erased.
    [[nodiscard]] TPorts::in_type& inputs() override {
        return io_.in;
    }
    [[nodiscard]] const TPorts::in_type& inputs() const override {
        return io_.in;
    }
    [[nodiscard]] TPorts::out_type& outputs() override {
        return io_.out;
    }
    [[nodiscard]] const TPorts::out_type& outputs() const override {
        return io_.out;
    }
    [[nodiscard]] TPorts::props_type& properties() override {
        return io_.props;
    }
    [[nodiscard]] const TPorts::props_type& properties() const override {
        return io_.props;
    }

   private:
    TPorts io_;
};

}  // namespace atp

#endif
