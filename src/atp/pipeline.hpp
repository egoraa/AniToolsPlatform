#ifndef ANITOOLSPLATFORM_PIPELINE_HPP
#define ANITOOLSPLATFORM_PIPELINE_HPP

#include <atp/group.hpp>
#include <atp/module_context.hpp>
#include <atp/service_directory.hpp>

namespace atp {

// Корень композиции: корневая группа + платформенные службы. Чистая
// структура без потоков — исполнение целиком в pipeline_runner.
// Порядок членов = порядок разрушения в обратную сторону: группа (и её
// соединения/модули) умирает раньше служб, на которые модули могли
// ссылаться в stop().
class pipeline {
   public:
    pipeline() = default;

    pipeline(const pipeline&) = delete;
    pipeline& operator=(const pipeline&) = delete;

    [[nodiscard]] group& root() {
        return root_;
    }
    [[nodiscard]] const group& root() const {
        return root_;
    }
    [[nodiscard]] service_directory& services() {
        return services_;
    }
    [[nodiscard]] module_context& context() {
        return context_;
    }

   private:
    service_directory services_;
    module_context context_{services_};
    group root_{"root"};
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_PIPELINE_HPP
