// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_ERROR_HPP
#define ATP_RUNTIME_CONFIG_ERROR_HPP

#include <stdexcept>

namespace atp::runtime {

/// Application-level error: reading, includes, building from a config.
///
/// It lives alone rather than beside the config model because catching the runtime's own failure is
/// something a host does without wanting the document machinery — and while the model named a JSON
/// library, a catch clause paid for one.
///
/// Its peer one layer down is atp::config::access_error: this one means the host could not read,
/// validate or build a config, that one means a single node or path within one is wrong.
class config_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

}  // namespace atp::runtime

#endif
