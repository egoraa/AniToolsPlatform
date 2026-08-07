// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_PIPELINE_HPP
#define ANITOOLSPLATFORM_PIPELINE_HPP

#include <string>
#include <string_view>
#include <vector>

#include <atp/group.hpp>
#include <atp/host_node.hpp>
#include <atp/module_context.hpp>
#include <atp/service_directory.hpp>

namespace atp {

/// Aggregate root of a composition: the root group plus the platform services. A pure structure
/// without threads — execution lives entirely in pipeline_runner.
class pipeline {
   public:
    pipeline() = default;

    pipeline(const pipeline&) = delete;
    pipeline& operator=(const pipeline&) = delete;

    /// Root group holding every module of the pipeline.
    [[nodiscard]] group& root() {
        return root_;
    }
    [[nodiscard]] const group& root() const {
        return root_;
    }

    /// Service directory shared by the modules.
    [[nodiscard]] service_directory& services() {
        return services_;
    }

    /// Context handed to the root group in initialize(); every child below gets one of its own,
    /// differing only in the host.
    [[nodiscard]] module_context& context() {
        return context_;
    }

    /// The root group's own host, for a caller wiring the log up.
    [[nodiscard]] host_node& root_host() {
        return host_;
    }

    /// Drains every module's log buffer, the root's own first.
    ///
    /// It empties what it reads, so exactly one caller may do this: two drains would each see a
    /// part of the log and neither would see it whole.
    /// @return the lines, with paths relative to the root
    [[nodiscard]] std::vector<log_line> collect_logs() {
        std::vector<log_line> out;
        host_.ring().drain([&out](log_level level, std::string_view text, bool truncated) {
            out.push_back({"root", level, std::string(text), truncated});
        });
        root_.collect_logs(std::string(), out);
        return out;
    }

   private:
    service_directory services_;
    host_node host_;
    module_context context_{services_, host_};
    group root_{"root"};
};

}  // namespace atp

#endif
