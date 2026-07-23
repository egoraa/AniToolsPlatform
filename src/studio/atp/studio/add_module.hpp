#ifndef ATP_STUDIO_ADD_MODULE_HPP
#define ATP_STUDIO_ADD_MODULE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <atp/studio/document.hpp>
#include <atp/version.hpp>

namespace atp::studio {

// Запрос на добавление модуля в группу. Отдельная структура, а не список
// аргументов: у добавления два вызывающих (двойной клик и сброс на канвас),
// и они отличаются ровно одним полем — position.
struct add_module_request {
    std::string group_path;                  // куда добавляем
    std::string factory;                     // имя фабрики
    std::optional<version> factory_version;  // nullopt — последняя
    std::filesystem::path plugin;            // DLL, откуда фабрика
    std::filesystem::path config_dir;        // каталог документа (пуст, если не сохранён)
    std::optional<node_position> position;   // nullopt — позиция по auto_layout
};

struct add_module_result {
    std::string name;     // фактическое имя узла: с суффиксом при коллизии
    std::string warning;  // непусто, когда путь плагина остался абсолютным
};

namespace detail {

// Имя нового узла: имя фабрики, при занятости — числовой суффикс.
[[nodiscard]] inline std::string unique_child_name(const document& doc,
                                                   const std::string& group_path,
                                                   const std::string& factory) {
    const runtime::group_node* g = doc.group_at(group_path);
    auto taken = [&](const std::string& name) {
        if (g == nullptr) {
            return false;  // группы нет — конфликтовать не с чем, ошибку даст add_module
        }
        for (const runtime::child_node& c : g->children) {
            if ((c.module ? c.module->name : c.group->name) == name) {
                return true;
            }
        }
        return false;
    };
    if (!taken(factory)) {
        return factory;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = factory + "_" + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

}  // namespace detail

// Добавляет модуль и всё, что к нему прилагается: запись DLL в plugins конфига
// (иначе модуль не запустится) и, если задана, позицию узла. Бросает
// runtime::config_error на тех же условиях, что document::add_module.
inline add_module_result add_module(document& doc, const add_module_request& request) {
    add_module_result result;
    result.name = detail::unique_child_name(doc, request.group_path, request.factory);
    doc.add_module(request.group_path, request.factory, result.name, request.factory_version);

    // Путь плагина по возможности относительный — документ должен оставаться
    // переносимым. relative() умеет и выход вверх через "..", а пустой отдаёт
    // лишь когда база пустая (документ не сохранён) или пути несовместимы
    // (разные корни на Windows) — вот тогда и предупреждаем.
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(request.plugin, request.config_dir, ec);
    std::string entry;
    if (!ec && !relative.empty()) {
        entry = relative.generic_string();
    } else {
        entry = request.plugin.generic_string();
        result.warning = "plugin path is absolute: " + entry;
    }
    doc.add_plugin(entry);

    if (request.position) {
        doc.set_position(detail::full_path(request.group_path, result.name), *request.position);
    }
    return result;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_ADD_MODULE_HPP
