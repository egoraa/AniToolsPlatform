#ifndef ANITOOLSPLATFORM_IO_THREADING_HPP
#define ANITOOLSPLATFORM_IO_THREADING_HPP

namespace atp::io {

// Потокобезопасность — свойство экземпляра входа, а не его типа.
// Выбирается в точке создания:
//     make<input<int>>("fast", unsafe)
struct safety {
    bool locking;
};

inline constexpr safety safe{true};
inline constexpr safety unsafe{false};

// Ответ опрашиваемого (module_base::iterate, watcher::poll): занимался ли
// работой. idle подряд у всех модулей потока — сигнал раннеру сбавить темп
// (сон с потолком); busy сбрасывает backoff.
enum class work_status { busy, idle };

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_THREADING_HPP
