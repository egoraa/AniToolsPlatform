#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include <string>
#include <typeindex>
#include <utility>

namespace atp::io {

    // Type-erased база: именно указатели на неё хранит реестр inputs.
    class input_base {
    public:
        input_base(std::string name, std::type_index type)
            : name_(std::move(name)), type_(type) {}
        virtual ~input_base() = default;

        input_base(const input_base&) = delete;
        input_base& operator=(const input_base&) = delete;

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] std::type_index type() const { return type_; }

        virtual void reset() = 0;

    private:
        std::string name_;
        std::type_index type_;  // typeid(std::tuple<Args...>) — источник истины
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
