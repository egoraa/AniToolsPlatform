// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_TREE_SOURCE_HPP
#define ATP_RUNTIME_CONFIG_TREE_SOURCE_HPP

#include <atp/config/node.hpp>

namespace atp::runtime {

/// What c_module needs of a config, whichever kind it was handed: the tree to index for the plugin.
///
/// A mixin with no data rather than a common base, because the two configs that carry a tree reach
/// module_config by different routes — raw_config directly, c_config through dynamic_config — and a
/// common base holding module_config would mean inheriting it twice.
class config_tree_source {
   public:
    config_tree_source() = default;
    virtual ~config_tree_source() = default;
    config_tree_source(const config_tree_source&) = delete;
    config_tree_source& operator=(const config_tree_source&) = delete;
    config_tree_source(config_tree_source&&) = delete;
    config_tree_source& operator=(config_tree_source&&) = delete;

    /// The document as the plugin reads it, stable for as long as this object lives — c_module keeps
    /// pointers into it for the module's whole life.
    [[nodiscard]] virtual const atp::config::node& tree() const noexcept = 0;
};

}  // namespace atp::runtime

#endif
