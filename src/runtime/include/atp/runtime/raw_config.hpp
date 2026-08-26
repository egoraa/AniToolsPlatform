// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_RAW_CONFIG_HPP
#define ATP_RUNTIME_RAW_CONFIG_HPP

#include <atp/config/node.hpp>
#include <atp/module/module_config.hpp>
#include <atp/runtime/config_path.hpp>
#include <atp/runtime/config_source.hpp>
#include <atp/runtime/config_tree_source.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace atp::runtime {

/// A config taken over whole, as the document gave it: the tree, with path access on top of it.
///
/// The config of a module that declares no field at all. The C path and both script bridges read a
/// tree rather than declared fields — the foreign side has no C++ type to bind references into — so
/// they are handed this instead of an heir of the base, and reach the document through root() and
/// find(). It lives host-side and not in the SDK because nothing a module author writes needs it:
/// a module written in C++ declares its fields, and a module that does not is described by the very
/// tree this holds.
///
/// The root is always an object or null, whatever spelling the author used — a file of a format the
/// host does not parse yields null rather than a string root, so a reader never has to check the form
/// of the root to find out which syntax was picked.
class raw_config : public atp::module_config, public config_tree_source {
   public:
    /// Takes the document over whole. The C path and the bridges read a tree rather than declared
    /// fields, so this config declares no field at all and carries the node instead.
    /// @param source config exactly as the document gave it
    void adopt(config_source source) {
        root_ = std::move(source.root);
        attach_source(std::move(source.text), std::move(source.origin), source.opaque);
    }

    [[nodiscard]] const atp::config::node& root() const noexcept {
        return root_;
    }

    /// The same node under the name c_module addresses every config by; see config_tree_source.
    [[nodiscard]] const atp::config::node& tree() const noexcept override {
        return root_;
    }

    /// Whether this config came from a file at all, which is exactly when text() and origin() mean
    /// anything. Derived from origin() rather than remembered: a config read from a file always
    /// carries that file's path, and the document layer decides "from a file" the same way.
    [[nodiscard]] bool from_file() const noexcept {
        return !origin().empty();
    }

    /// Node at @p path, or nullptr when there is nothing there.
    ///
    /// Grammar: `path := segment ('.' segment)*`, `segment := name index* | index+`,
    /// `index := '[' digits ']'`. A nameless segment is legal only as the very first one, so
    /// "[0].rate" addresses an array root while "a.[0]" is a mistake. A key containing '.' or '[' is
    /// unreachable this way and stays reachable through root().
    ///
    /// **A malformed path throws rather than answering nullptr**, which is why this is not noexcept:
    /// the path is written by the module's own author in their own source, and turning a typo into
    /// "nothing there" turns it into an hour of debugging. nullptr is the other failure — absence.
    ///
    /// It **hides** module_config::find, which looks a declared field up by name, and that is meant:
    /// this config declares no field, so the inherited one could only ever answer nullptr, while a
    /// path is the one lookup that means anything here. The two take the same argument type, so
    /// bringing the base's in with `using` would make every call ambiguous rather than useful.
    /// @throws atp::config::access_error naming the path and the offset of whatever broke the grammar
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    [[nodiscard]] const atp::config::node* find(std::string_view path) const {
        return find_path(root_, path);
    }

    /// @throws atp::config::access_error on a malformed path, or naming the full path and where the walk stopped
    [[nodiscard]] const atp::config::node& at(std::string_view path) const {
        return at_path(root_, path);
    }

    [[nodiscard]] bool contains(std::string_view path) const {
        return find(path) != nullptr;
    }

   private:
    atp::config::node root_;
};

}  // namespace atp::runtime

#endif
