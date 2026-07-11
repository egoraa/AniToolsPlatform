#ifndef ANITOOLSPLATFORM_PLUGIN_HPP
#define ANITOOLSPLATFORM_PLUGIN_HPP

#include <atp/module_registry.hpp>

namespace atp {

// ABI платформы: растёт при несовместимом изменении module_base или
// фабричных интерфейсов. Не защищает от чужого компилятора/рантайма —
// требование сборки плагинов: тот же тулчейн, что у хоста (MSVC: общий
// CRT, /MD).
inline constexpr unsigned plugin_abi = 1;

// Контракт плагина — два C-символа. Рукопожатие atp_abi_version —
// единственный вызов, безопасный при любом рассогласовании: чистый C,
// без параметров и C++-типов. Регистрация — уже C++-вызов, разрешён
// только после совпадения ABI.
//     ATP_PLUGIN_EXPORT unsigned atp_abi_version();
//     ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar&);
using abi_version_fn = unsigned();
using register_modules_fn = void(module_registrar&);

// Имена символов — константами, чтобы загрузчик и тесты не дублировали строки.
inline constexpr const char* abi_version_symbol = "atp_abi_version";
inline constexpr const char* register_modules_symbol = "atp_register_modules";

}  // namespace atp

// Кроссплатформенный экспорт C-символа из плагина.
#if defined(_WIN32)
#define ATP_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define ATP_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#endif  // ANITOOLSPLATFORM_PLUGIN_HPP
