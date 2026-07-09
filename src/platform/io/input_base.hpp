#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include <mutex>
#include <string>
#include <typeindex>
#include <utility>

#include "threading.hpp"

namespace atp::io {

    // Type-erased база: именно указатели на неё хранит реестр inputs.
    // Владеет синхронизацией: мьютекс общий для всех видов входов,
    // блокировка включается флагом (см. safety) в момент создания.
    class input_base {
    public:
        input_base(std::string name, std::type_index type, safety s)
            : name_(std::move(name)), type_(type), locking_(s.locking) {}
        virtual ~input_base() = default;

        input_base(const input_base&) = delete;
        input_base& operator=(const input_base&) = delete;

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] std::type_index type() const { return type_; }

        virtual void reset() = 0;

    protected:
        // «Пустой» замок через defer_lock: его деструктор ничего не
        // разблокирует, поэтому unsafe-вход платит только за branch.
        [[nodiscard]] std::unique_lock<std::mutex> lock() const {
            return locking_ ? std::unique_lock(mutex_)
                            : std::unique_lock(mutex_, std::defer_lock);
        }

    private:
        std::string name_;
        std::type_index type_;  // typeid(T) — источник истины
        mutable std::mutex mutex_;
        const bool locking_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
