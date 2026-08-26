// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_C_CONFIG_HPP
#define ATP_RUNTIME_C_CONFIG_HPP

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <atp/plugin_c.h>
#include <atp/config/access_error.hpp>
#include <atp/config/node.hpp>
#include <atp/module/dynamic_config.hpp>
#include <atp/runtime/config_binding.hpp>
#include <atp/runtime/config_tree_source.hpp>

namespace atp::runtime {

/// The config of a module of the C path that declares its fields.
///
/// The counterpart of raw_config, which is what a module declaring none still gets. Both carry a tree
/// for the plugin to read; this one builds that tree out of its own filled fields instead of adopting
/// the document, which is what puts every declared key in front of the module at its own default.
class c_config final : public atp::dynamic_config, public config_tree_source {
   public:
    /// @param fields the module's declaration, which has to stay valid only for this call — everything
    ///        is copied into the entries
    /// @throws config::access_error naming the field if a default does not parse or is outside its
    ///         options, or if the declaration names a form outside the enumeration
    explicit c_config(std::span<const atp_config_field_desc> fields) {
        declare_into(*this, fields);
    }

    /// Renders the filled fields into the tree the plugin reads, defaults included.
    ///
    /// Called once, after load_fields and before c_module indexes it. Deliberately not lazy: c_module
    /// keeps pointers into the tree for the module's whole life, so it has to stop changing before the
    /// first of them is taken.
    void materialize() {
        tree_ = values_of(*this);
    }

    [[nodiscard]] const atp::config::node& tree() const noexcept override {
        return tree_;
    }

   private:
    static void declare_into(atp::dynamic_config& target, std::span<const atp_config_field_desc> fields) {
        for (const atp_config_field_desc& f : fields) {
            declare_one(target, f);
        }
    }

    static void declare_one(atp::dynamic_config& target, const atp_config_field_desc& f) {
        std::string name = f.name == nullptr ? std::string() : std::string(f.name);
        std::vector<std::string> options = read_options(f);
        const std::span<const atp_config_field_desc> children(f.fields, f.fields == nullptr ? 0 : f.field_count);

        if (f.kind == ATP_FIELD_OBJECT) {
            declare_into(target.object(std::move(name)), children);
            return;
        }
        if (f.kind == ATP_FIELD_ARRAY) {
            if (f.element == ATP_FIELD_OBJECT) {
                target.object_list(std::move(name),
                                   [children](atp::dynamic_config& element) { declare_into(element, children); });
                return;
            }
            target.scalar_list(std::move(name), to_field_kind(f.element), std::move(options));
            return;
        }
        if (f.default_value == nullptr) {
            target.required_scalar(std::move(name), to_field_kind(f.kind), std::move(options));
            return;
        }
        target.scalar(std::move(name), to_field_kind(f.kind), std::string(f.default_value), std::move(options));
    }

    [[nodiscard]] static std::vector<std::string> read_options(const atp_config_field_desc& f) {
        std::vector<std::string> options;
        if (f.options == nullptr) {
            return options;
        }
        options.reserve(f.option_count);
        for (std::uint32_t i = 0; i < f.option_count; ++i) {
            options.emplace_back(f.options[i] == nullptr ? "" : f.options[i]);
        }
        return options;
    }

    [[nodiscard]] static atp::field_kind to_field_kind(atp_config_field_kind kind) {
        switch (kind) {
            case ATP_FIELD_BOOL:
                return atp::field_kind::boolean;
            case ATP_FIELD_INT:
                return atp::field_kind::integer;
            case ATP_FIELD_REAL:
                return atp::field_kind::real;
            case ATP_FIELD_STRING:
                return atp::field_kind::string;
            case ATP_FIELD_OBJECT:
                return atp::field_kind::object;
            case ATP_FIELD_ARRAY:
                return atp::field_kind::array;
        }
        throw atp::config::access_error("config field kind " + std::to_string(static_cast<int>(kind)) +
                                        " is outside the enumeration");
    }

    atp::config::node tree_;
};

}  // namespace atp::runtime

#endif
