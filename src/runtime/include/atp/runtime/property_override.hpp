#ifndef ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
#define ATP_RUNTIME_PROPERTY_OVERRIDE_HPP

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>

namespace atp::runtime {

// Правка одной проперти по пути в дереве групп: источник — флаг -p у
// atp_app ("group.module.prop=value") и правки на лету из studio.
struct property_override {
    std::string module_path;  // путь модуля в дереве групп, сегменты через '.'
    std::string name;         // имя проперти
    std::string value;        // строковое значение (парсит сама проперть)
};

// Разбор "path.prop=value": по ПЕРВОМУ '=' (значение может содержать '='),
// слева — по ПОСЛЕДНЕЙ '.' (имя проперти точку содержать не может, путь —
// может). Ошибки формата — config_error с исходной строкой.
[[nodiscard]] inline property_override parse_property_override(std::string_view arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    const std::string_view left = arg.substr(0, eq);
    const std::size_t dot = left.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == left.size()) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    return {std::string(left.substr(0, dot)), std::string(left.substr(dot + 1)), std::string(arg.substr(eq + 1))};
}

// Спуск по дереву: сегменты до последнего — группы, последний — модуль.
// Все отказы — config_error с полным путём: пользователь видит, что именно
// не нашлось. Переиспользуется и записью (apply), и чтением (инспектор).
[[nodiscard]] inline io::property_base& find_property(group& root,
                                                      std::string_view module_path,
                                                      const std::string& name) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = module_path.find('.', begin);
        if (dot == std::string_view::npos) {
            break;
        }
        const std::string segment(module_path.substr(begin, dot - begin));
        group* next = current->find_group(segment);
        if (next == nullptr) {
            throw config_error("property override: no group '" + segment + "' in path '" + std::string(module_path) +
                               "'");
        }
        current = next;
        begin = dot + 1;
    }
    const std::string module_name(module_path.substr(begin));
    module_base* m = current->find_module(module_name);
    if (m == nullptr) {
        throw config_error("property override: no module at path '" + std::string(module_path) + "'");
    }
    io::property_base* prop = m->properties().find(name);
    if (prop == nullptr) {
        throw config_error("property override: module '" + std::string(module_path) + "' has no property '" + name +
                           "'");
    }
    return *prop;
}

// Непарсящееся значение — тоже ошибка конфигурации, не логики.
inline void apply_property_override(group& root, const property_override& o) {
    io::property_base& prop = find_property(root, o.module_path, o.name);
    try {
        prop.from_string(o.value);
    } catch (const std::invalid_argument& e) {
        throw config_error(std::string("property override: ") + e.what());
    }
}

}  // namespace atp::runtime

#endif  // ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
