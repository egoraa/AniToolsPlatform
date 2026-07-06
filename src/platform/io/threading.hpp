#ifndef ANITOOLSPLATFORM_IO_THREADING_HPP
#define ANITOOLSPLATFORM_IO_THREADING_HPP

namespace atp::io {

    // Пустой мьютекс для однопоточных входов: удовлетворяет Lockable,
    // компилятор выкидывает lock/unlock целиком — ноль накладных расходов.
    struct null_mutex {
        void lock() noexcept {}
        void unlock() noexcept {}
        bool try_lock() noexcept { return true; }
    };

    // Тег для выбора однопоточного варианта входа в точке сборки пайплайна:
    //     make<int>("fast", unsafe)
    struct unsafe_t {
        explicit unsafe_t() = default;
    };
    inline constexpr unsafe_t unsafe{};

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_THREADING_HPP
