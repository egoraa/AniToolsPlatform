// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_TESTS_SUPPORT_REQUIRED_HPP
#define ATP_TESTS_SUPPORT_REQUIRED_HPP

#include <optional>
#include <stdexcept>

namespace atp_tests {

/// The value of an optional a test expects to be engaged.
///
/// It exists because the two obvious spellings are both wrong. `*x` and `x.value()` read as checked
/// but are not — a test that lost its subject then dies inside an assertion about something else.
/// `ASSERT_TRUE(x.has_value())` before the dereference reads as a check but frequently is not: when
/// the optional comes back from a call, the assertion inspects one result and the dereference uses
/// the next one.
///
/// Throwing rather than asserting keeps the call sites one-liners; gtest reports the exception
/// against the test that caused it, which is all the diagnosis this needs.
/// @throws std::logic_error if the optional is empty
template <typename T>
[[nodiscard]] const T& required(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error("expected an engaged optional");
    }
    return *value;
}

}  // namespace atp_tests

#endif
