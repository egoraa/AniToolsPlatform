// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_SCALAR_HPP
#define ANITOOLSPLATFORM_CONFIG_SCALAR_HPP

#include <concepts>
#include <cstdint>
#include <string>

namespace atp::config {

/// The four scalar forms a config value can take. Its own header rather than a line in read.hpp,
/// because module_config names it and must not drag the document type along with it.
template <typename T>
concept scalar =
    std::same_as<T, bool> || std::same_as<T, std::int64_t> || std::same_as<T, double> || std::same_as<T, std::string>;

}  // namespace atp::config

#endif
