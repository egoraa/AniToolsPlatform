#ifndef ANITOOLSPLATFORM_PLUGIN_HPP
#define ANITOOLSPLATFORM_PLUGIN_HPP

#include <atp/module_registry.hpp>

namespace atp {

// ABI платформы: растёт при несовместимом изменении контрактов, которые
// видит плагин (module_base, фабрики, io-типы). Не защищает от чужого
// компилятора/рантайма — требование сборки плагинов: тот же тулчейн,
// что у хоста (MSVC: общий CRT, /MD).
// 2: initialize/start/stop принимают module_context&.
// 3: pull-модель входов (io: -when/+take/watcher); create() возвращает
//    module_ptr (пин библиотеки в делетере); module_base отдаёт io-реестры
//    (inputs()/outputs()); iterate возвращает work_status (контракт
//    простоя для исполнителя).
// 4: start()/stop() без параметров — module_context& даётся только в
//    initialize (кому нужен позже — сохраняет ссылку).
// 5: NVI-доставка входа (do_deliver) + notifier_base/set_notifier у
//    input_base — пробуждение потока-потребителя исполнителем.
inline constexpr unsigned plugin_abi = 5;

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
