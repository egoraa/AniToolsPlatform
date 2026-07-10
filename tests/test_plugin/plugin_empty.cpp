#include <atp/plugin.hpp>

// Библиотека без контракта atp: загрузчик должен сообщить об отсутствии
// atp_abi_version. Один посторонний экспорт гарантирует генерацию DLL на MSVC.
ATP_PLUGIN_EXPORT void unrelated_symbol() {}
