// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/option_set.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// Persistence of a property instance, in the style of safety: studio writes persistent values
/// into the config when saving, transient ones live only in the memory of a running pipeline.
struct persistence {
    bool keep;
};

/// The value is written to the config on save.
inline constexpr persistence persistent{true};

/// The value lives only while the pipeline runs.
inline constexpr persistence transient{false};

/// Type-erased base of a property, the peer of input_base/output_base: name, value type and
/// synchronisation come from io_base, while string access (the builder, the CLI and studio edit a
/// value without knowing T), the value kind and the persistence flag live here. Properties never
/// connect to outputs — they are a separate kind of entity with a registry of their own.
class property_base : public io_base {
   public:
    /// @param name property name, unique within its registry
    /// @param type typeid of the value
    /// @param kind JSON type of the value
    /// @param options allowed values in canonical string form, empty if unconstrained
    /// @param p whether the value is written to the config on save
    /// @param s whether this instance serialises access
    property_base(std::string name,
                  std::type_index type,
                  property_kind kind,
                  std::vector<std::string> options,
                  persistence p,
                  safety s)
        : io_base(std::move(name), type, s), kind_(kind), options_(std::move(options)), persistent_(p.keep) {}

    /// JSON type of the value, independent of the value set.
    [[nodiscard]] property_kind kind() const noexcept {
        return kind_;
    }

    /// Allowed values in canonical string form, in declaration order. Empty means unconstrained;
    /// non-empty makes this an enumeration property — the inspector draws a drop-down and a write
    /// outside the set is rejected. Where the set came from (the name table of an enum type or the
    /// instance's own option_set) is of no concern here: both paths have already converged.
    [[nodiscard]] const std::vector<std::string>& options() const noexcept {
        return options_;
    }

    /// Whether the value is written to the config on save.
    [[nodiscard]] bool persistent() const noexcept {
        return persistent_;
    }

    /// Current value in canonical string form.
    [[nodiscard]] virtual std::string to_string() const = 0;

    /// Parses @p text and writes the result.
    /// @throws std::invalid_argument on an unparsable or disallowed string, naming the property and
    ///         quoting the string, without touching either the value or the changed flag
    virtual void from_string(std::string_view text) = 0;

    /// Default value in string form: studio compares the current value against it when saving and
    /// omits the equal ones, keeping diffs quiet.
    [[nodiscard]] virtual std::string default_string() const = 0;

    /// Non-destructive peek at "changed since the last take". Taking itself is the heir's typed
    /// take(), which makes no sense without T.
    [[nodiscard]] virtual bool changed() const = 0;

   private:
    property_kind kind_;
    std::vector<std::string> options_;
    bool persistent_;
};

}  // namespace atp::io

#endif
