// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_LOG_LEVEL_HPP
#define ANITOOLSPLATFORM_MODULE_LOG_LEVEL_HPP

#include <cstdint>

namespace atp {

/// Severity of a log line. Ordered from the most to the least important, which is the order the
/// hosts' thresholds compare against: a threshold admits every level up to and including itself.
///
/// Apart from module_host because it is reached far more widely than the host interface is: the log
/// ring, the pump, the MCP options and studio's log dock all name a level without ever calling log().
enum class log_level : std::uint8_t { error, warning, info, debug };

}  // namespace atp

#endif
