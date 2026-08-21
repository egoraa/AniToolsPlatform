// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_HOSTING_NULL_HOST_HPP
#define ANITOOLSPLATFORM_HOSTING_NULL_HOST_HPP

#include <string_view>

#include <atp/module/module_host.hpp>

namespace atp {

/// A module_host that discards everything: the value a context needs when nobody is listening.
///
/// It exists for the tests that build a module without a pipeline, and for a host that has not
/// wired the log up — a module must be able to say something into the void without checking first
/// whether anyone is there.
class null_host final : public module_host {
   public:
    void log(log_level, std::string_view) noexcept override {}

    void wake() noexcept override {}
};

}  // namespace atp

#endif
