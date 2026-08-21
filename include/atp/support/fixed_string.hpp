// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_SUPPORT_FIXED_STRING_HPP
#define ANITOOLSPLATFORM_SUPPORT_FIXED_STRING_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace atp::detail {

/// A string literal usable as a non-type template parameter, which is what lets a module declare its
/// name (module.hpp) and a version be spelled as text (ver<"1.2.3">). Lives apart from both, because
/// it is about strings and knows nothing of either.
template <std::size_t N>
struct fixed_string {
    std::array<char, N> chars{};

    /// Takes a raw array rather than a std::array on purpose: a literal is the only way to spell a
    /// string as a template value parameter, and std::array is not deducible from one.
    ///
    /// @param text string literal, the terminating NUL included in N
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    constexpr fixed_string(const char (&text)[N]) {
        std::ranges::copy(text, chars.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const {
        return {chars.data(), N - 1};
    }
};

}  // namespace atp::detail

#endif
