#ifndef ANITOOLSPLATFORM_IO_IO_BASE_HPP
#define ANITOOLSPLATFORM_IO_IO_BASE_HPP

#include <mutex>
#include <string>
#include <typeindex>
#include <utility>

#include <atp/io/threading.hpp>

namespace atp::io {

// Общая type-erased база входов и выходов: имя + typeid(T) + синхронизация.
// Владеет синхронизацией: мьютекс общий для всех видов элементов,
// блокировка включается флагом (см. safety) в момент создания.
// Тонкие наследники input_base/output_base существуют ради типового
// различия: реестр входов физически не может принять выход и наоборот.
class io_base {
   public:
    io_base(std::string name, std::type_index type, safety s)
        : name_(std::move(name)), type_(type), locking_(s.locking) {}
    virtual ~io_base() = default;

    io_base(const io_base&) = delete;
    io_base& operator=(const io_base&) = delete;

    [[nodiscard]] const std::string& name() const {
        return name_;
    }
    [[nodiscard]] std::type_index type() const {
        return type_;
    }

    virtual void reset() = 0;

   protected:
    // «Пустой» замок через defer_lock: его деструктор ничего не
    // разблокирует, поэтому unsafe-элемент платит только за branch.
    [[nodiscard]] std::unique_lock<std::mutex> lock() const {
        return locking_ ? std::unique_lock(mutex_) : std::unique_lock(mutex_, std::defer_lock);
    }

   private:
    std::string name_;
    std::type_index type_;  // typeid(T) — источник истины
    mutable std::mutex mutex_;
    const bool locking_;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_IO_BASE_HPP
