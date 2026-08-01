#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>
#include <string_view>
#include <utility>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

/// Base class of every concrete module: implements module_base and holds the port node.
///
/// Ports are declared as a single io::ports node and passed as one parameter; the name comes
/// second and the version third, because the name is needed more often and C++ has no way to skip
/// a default. An empty name means anonymous: such a module can only be registered under an
/// explicit name (module_registry::add<M>(name)).
/// @tparam TPorts port node gathering the inputs, outputs and properties sections
/// @tparam Name module name, empty for an anonymous module
/// @tparam Version module version
template <io::ports_node TPorts = io::ports<>, detail::fixed_string Name = "", version Version = default_version>
class module : public module_base {
   public:
    static constexpr std::string_view module_name = Name.view();
    static constexpr version module_version = Version;

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
