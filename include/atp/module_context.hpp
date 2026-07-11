#ifndef ANITOOLSPLATFORM_MODULE_CONTEXT_HPP
#define ANITOOLSPLATFORM_MODULE_CONTEXT_HPP

#include <atp/service_directory.hpp>

namespace atp {

// Контекст, который платформа передаёт модулю в initialize/start/stop:
// агрегат ссылок на службы платформы. Новые службы добавляются полями —
// сигнатуры жизненного цикла при этом не меняются (но раскладка контекста
// видна плагинам, поэтому добавление поля — рост plugin_abi).
// Полный include справочника, а не forward-declaration: контекст без
// полного типа своей единственной службы бесполезен.
struct module_context {
    service_directory& services;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_CONTEXT_HPP
