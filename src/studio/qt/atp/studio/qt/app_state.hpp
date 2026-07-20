#ifndef ATP_STUDIO_QT_APP_STATE_HPP
#define ATP_STUDIO_QT_APP_STATE_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include <QString>

#include <atp/studio/document.hpp>
#include <atp/studio/module_manager.hpp>
#include <atp/studio/session.hpp>
#include <atp/studio/settings.hpp>
#include <atp/version.hpp>

namespace atp::studio::qt {

// Всё состояние приложения одним агрегатом; виджеты — тонкие обёртки над
// ним. Порядок членов = порядок разрушения в обратную сторону: сессия
// умирает раньше менеджера (её пайплайн держит модули из DLL менеджера).
struct app_state {
    studio_settings settings;
    std::filesystem::path settings_file = default_settings_path();
    module_manager manager;
    document doc = document::create();
    std::optional<std::filesystem::path> doc_path;  // nullopt — новый безымянный
    session run{manager.registry()};

    std::string current_group;   // путь группы, показанной канвасом ("" — корень)
    std::string selected_child;  // выбранный ребёнок текущей группы; "" — нет

    // Кэш описаний модулей (порты пробным экземпляром — не дёргать фабрику
    // на каждую перестройку сцены). Ключ — "фабрика@версия".
    std::unordered_map<std::string, module_info> describe_cache;

    [[nodiscard]] std::filesystem::path config_dir() const {
        return doc_path ? doc_path->parent_path() : std::filesystem::current_path();
    }

    [[nodiscard]] const module_info* describe_cached(const std::string& factory, const std::optional<version>& ver) {
        const std::string key = factory + "@" + (ver ? ver->to_string() : "latest");
        auto it = describe_cache.find(key);
        if (it != describe_cache.end()) {
            return &it->second;
        }
        const module_factory_base* f = ver ? manager.registry().find(factory, *ver) : manager.registry().find(factory);
        if (f == nullptr) {
            return nullptr;  // фабрики нет (плагин не загружен) — узел без портов
        }
        return &describe_cache.emplace(key, module_manager::describe(*f)).first->second;
    }

    void reset_view() {
        current_group.clear();
        selected_child.clear();
    }
};

// Коллбэки вместо сигналов: виджеты без Q_OBJECT/moc — header-only, как
// весь проект. document_changed перестраивает зависимые виджеты; error
// пишет в журнал; selection_changed обновляет инспектор.
struct ui_callbacks {
    std::function<void()> document_changed;
    std::function<void(const QString&)> error;
    std::function<void()> selection_changed;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_APP_STATE_HPP
