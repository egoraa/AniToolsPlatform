// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_MODULE_CONFIG_HPP
#define ANITOOLSPLATFORM_MODULE_MODULE_CONFIG_HPP

#include <atp/config/node.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace atp {

/// A module's whole config, as its constructor receives it: the tree it was given, and — when it came
/// from a file — the bytes of that file and the file's name.
///
/// Distinct from config::node, which describes one *node* of that tree and cannot carry what a whole
/// config has: path access, the text, its origin.
///
/// The root is always an object or null, whatever spelling the author used — a file of a format the
/// host does not parse yields null rather than a string root, so a module never has to check the form
/// of the root to find out which syntax was picked.
///
/// **A constructor taking one should tolerate the null form**, falling back to defaults rather than
/// throwing, simply because a module may be given no config at all — a node that named none, or a host
/// that has none to give.
///
/// It used to be a harder rule than that, and the reason is worth knowing because the old wording
/// outlived it: a host once built a **probe instance** with an empty config to learn a module's ports,
/// so a constructor that threw made the module undescribable and therefore unplaceable. That is gone.
/// A module written against module<> is described from its declared port node without being built at
/// all, and one that declares a config_type has its config checked before its constructor runs. The
/// probe survives only for a module written by hand from module_base, which names no port node — there,
/// and only there, a throwing constructor still costs the module its description.
class module_config {
   public:
    /// No config was named: null root, no text.
    module_config() = default;

    /// A config that came as a tree and nothing else — raw text can only come from a file.
    explicit module_config(config::node root) : root_(std::move(root)) {}

    /// A config read from a file the host could parse.
    module_config(config::node root, std::string text, std::string origin)
        : root_(std::move(root)), text_(std::move(text)), origin_(std::move(origin)), from_file_(true) {}

    /// A config read from a file of a format the host does not parse: the module gets the bytes and
    /// parses them itself.
    [[nodiscard]] static module_config opaque(std::string text, std::string origin) {
        module_config cfg;
        cfg.text_ = std::move(text);
        cfg.origin_ = std::move(origin);
        cfg.from_file_ = true;
        cfg.opaque_ = true;
        return cfg;
    }

    [[nodiscard]] const config::node& root() const noexcept {
        return root_;
    }

    /// Bytes of the file this config came from, verbatim and including a BOM; empty when it did not
    /// come from a file. Declared UTF-8 like every other string crossing an ABI here, and neither
    /// validated nor transcoded.
    [[nodiscard]] const std::string& text() const noexcept {
        return text_;
    }

    /// Path of that file, so a module parsing the text itself can say "rig.yaml:12: ..." and can
    /// resolve paths written *inside* its config against it.
    ///
    /// Named origin rather than source because module_info::source already means the file a module is
    /// *declared* in, which is a different file.
    [[nodiscard]] const std::string& origin() const noexcept {
        return origin_;
    }

    /// Whether the text is all there is. Not derivable from the rest: a parsed file holding literally
    /// `null` also leaves an empty root beside a non-empty text, and a module deciding whether to parse
    /// the text itself would get that case wrong.
    [[nodiscard]] bool is_opaque() const noexcept {
        return opaque_;
    }

    /// Whether this config came from a file at all, which is exactly when text() and origin() mean
    /// anything.
    [[nodiscard]] bool from_file() const noexcept {
        return from_file_;
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
    /// @throws config::access_error naming the path and the offset of whatever broke the grammar
    [[nodiscard]] const config::node* find(std::string_view path) const {
        return walk(path, nullptr);
    }

    /// @throws config::access_error on a malformed path, or naming the full path and where the walk stopped
    [[nodiscard]] const config::node& at(std::string_view path) const {
        path_stop stop;
        if (const config::node* found = walk(path, &stop)) {
            return *found;
        }
        throw config::access_error(stop_message(path, stop));
    }

    [[nodiscard]] bool contains(std::string_view path) const {
        return find(path) != nullptr;
    }

   private:
    /// Where a lookup gave up, so at() can say it in one sentence and find() can ignore it.
    struct path_stop {
        std::size_t resolved = 0;
        config::kind kind = config::kind::null;
        std::string_view missing_key;
        bool out_of_range = false;
        std::size_t index = 0;
        std::size_t size = 0;
    };

    /// Parses @p path whole and resolves as far as the data allows.
    ///
    /// Parsing deliberately does not stop where resolving does: a lookup that finds nothing only sets
    /// @p stop and leaves the walk without a node, while the grammar is checked to the end of the
    /// string. Otherwise the same malformed path would throw against one config and answer "not there"
    /// against another, making the report of a typo depend on the data.
    [[nodiscard]] const config::node* walk(std::string_view path, path_stop* stop) const {
        if (path.empty()) {
            throw config::access_error(path_message(path, "empty", 0));
        }
        const config::node* found = &root_;
        std::size_t pos = 0;
        std::size_t resolved = 0;
        bool first = true;
        while (true) {
            const std::size_t name_begin = pos;
            while (pos < path.size() && path[pos] != '.' && path[pos] != '[') {
                ++pos;
            }
            const std::string_view name = path.substr(name_begin, pos - name_begin);
            if (name.empty() && !(first && pos < path.size() && path[pos] == '[')) {
                throw config::access_error(path_message(path, "empty key", name_begin));
            }
            if (!name.empty() && found != nullptr) {
                const config::node* child = found->find(name);
                if (child == nullptr) {
                    if (stop != nullptr) {
                        *stop = {resolved, found->kind(), found->is_object() ? name : std::string_view{}, false, 0, 0};
                    }
                    found = nullptr;
                } else {
                    found = child;
                    resolved = pos;
                }
            }
            while (pos < path.size() && path[pos] == '[') {
                const std::size_t bracket = pos;
                ++pos;
                const std::size_t digits = pos;
                std::size_t index = 0;
                while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') {
                    const std::size_t digit = static_cast<std::size_t>(path[pos] - '0');
                    if (index > (static_cast<std::size_t>(-1) - digit) / 10) {
                        throw config::access_error(path_message(path, "index too large", digits));
                    }
                    index = index * 10 + digit;
                    ++pos;
                }
                if (pos == digits) {
                    throw config::access_error(path_message(path, "'[' without a number", bracket));
                }
                if (pos == path.size() || path[pos] != ']') {
                    throw config::access_error(path_message(path, "unclosed '['", bracket));
                }
                ++pos;
                if (found == nullptr) {
                    continue;
                }
                if (!found->is_array() || index >= found->size()) {
                    if (stop != nullptr) {
                        *stop = {resolved, found->kind(), {}, true, index, found->size()};
                    }
                    found = nullptr;
                } else {
                    found = &(*found)[index];
                    resolved = pos;
                }
            }
            if (pos == path.size()) {
                return found;
            }
            if (path[pos] != '.') {
                throw config::access_error(path_message(path, "expected '.'", pos));
            }
            ++pos;
            if (pos == path.size()) {
                throw config::access_error(path_message(path, "trailing '.'", pos - 1));
            }
            first = false;
        }
    }

    [[nodiscard]] static std::string path_message(std::string_view path, std::string_view what, std::size_t at) {
        return "config: bad path '" + std::string(path) + "' (" + std::string(what) + " at " + std::to_string(at) + ")";
    }

    [[nodiscard]] static std::string stop_message(std::string_view path, const path_stop& stop) {
        const std::string where =
            stop.resolved == 0 ? "the root" : "'" + std::string(path.substr(0, stop.resolved)) + "'";
        const std::string prefix = "config: '" + std::string(path) + "' ";
        if (stop.out_of_range) {
            return prefix + "has no index " + std::to_string(stop.index) + " in " + where + " (size " +
                   std::to_string(stop.size) + ")";
        }
        if (!stop.missing_key.empty()) {
            return prefix + "has no key '" + std::string(stop.missing_key) + "' in " + where;
        }
        return prefix + "stops at " + where + " (" + std::string(config::node::kind_name(stop.kind)) + ")";
    }

    config::node root_;
    std::string text_;
    std::string origin_;
    bool from_file_ = false;
    bool opaque_ = false;
};

}  // namespace atp

#endif
