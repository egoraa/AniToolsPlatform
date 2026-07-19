#ifndef ANITOOLSPLATFORM_MODULE_CONFIG_HPP
#define ANITOOLSPLATFORM_MODULE_CONFIG_HPP

#include <string>

namespace atp {

// Конфиг экземпляра модуля — пока заглушка с сырым текстом (узел params
// конфига пайплайна как есть). Модуль с параметрами объявляет конструктор
// с module_config ПЕРВЫМ аргументом — фабрика передаёт его при
// create(config), связанные при регистрации аргументы идут следом.
// Позже здесь появятся обработчики «строка → типизированная структура»;
// контракт конструктора модуля при этом не изменится.
struct module_config {
    std::string raw;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_CONFIG_HPP
