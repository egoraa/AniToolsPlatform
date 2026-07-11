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

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_THREADING_HPP
