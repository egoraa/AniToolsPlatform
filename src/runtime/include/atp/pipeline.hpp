#ifndef ANITOOLSPLATFORM_PIPELINE_HPP
#define ANITOOLSPLATFORM_PIPELINE_HPP

#include <atp/group.hpp>
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

    /// Context handed to the modules in initialize().
    [[nodiscard]] module_context& context() {
        return context_;
    }

   private:
    service_directory services_;
    module_context context_{services_};
    group root_{"root"};
};

}  // namespace atp

#endif
