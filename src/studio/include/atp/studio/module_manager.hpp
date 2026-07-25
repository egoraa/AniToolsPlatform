#ifndef ATP_STUDIO_MODULE_MANAGER_HPP
#define ATP_STUDIO_MODULE_MANAGER_HPP

#include <algorithm>
#include <filesystem>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/io/property_base.hpp>
#include <atp/module_loader.hpp>
#include <atp/module_registry.hpp>

namespace atp::studio {

struct port_info {
    std::string name;
    std::type_index type;
};

// Описание проперти: всё, что инспектору нужно для виджета и обратной
// конвертации введённого в JSON. Набор копируется — описание переживает
// временный модуль-зонд, с которого снято.
struct property_info {
    std::string name;
    io::property_kind kind;            // подсказка виджету инспектора
    std::string default_value;         // дефолт строкой — сравнение при сохранении
    std::vector<std::string> options;  // непусто у перечислений: элементы выпадающего списка
    bool persistent = true;
};

struct module_info {
    std::string name;
    version ver;
    std::vector<port_info> inputs;
    std::vector<port_info> outputs;
    std::vector<property_info> properties;
    bool broken = false;  // пробный экземпляр не создался
    std::string error;
};

struct plugin_info {
    std::filesystem::path path;
    bool loaded = false;
    std::string error;  // причина отказа — пользователю, не в лог
    std::vector<module_info> modules;
};

// Сессионный владелец реестра модулей и загрузчиков DLL: палитра studio —
// его витрина. Папки поиска задаёт пользователь (настройки), плагины
// конфига загружаются теми же путями (load_plugin).
class module_manager {
   public:
    [[nodiscard]] module_registry& registry() {
        return registry_;
    }

    void add_search_dir(std::filesystem::path dir) {
        if (std::ranges::find(search_dirs_, dir) == search_dirs_.end()) {
            search_dirs_.push_back(std::move(dir));
        }
    }

    // Уже загруженные из папки DLL остаются: открытый документ может
    // ссылаться на их модули — выгрузка из-под него хуже лишней записи.
    bool remove_search_dir(const std::filesystem::path& dir) {
        return std::erase(search_dirs_, dir) > 0;
    }

    [[nodiscard]] const std::vector<std::filesystem::path>& search_dirs() const {
        return search_dirs_;
    }

    // Скан папок: новые файлы загружаются, прежние отказы пробуются снова
    // (файл могли пересобрать); успешно загруженное не трогается.
    void rescan() {
        for (const std::filesystem::path& dir : search_dirs_) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_regular_file() && is_plugin_file(entry.path())) {
                    load_plugin(entry.path());
                }
            }
        }
    }

    // Явная загрузка одного файла (плагины конфига). Уже загруженный
    // путь — no-op; прежний отказ — новая попытка с обновлением записи.
    void load_plugin(const std::filesystem::path& file) {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(file);
        plugin_info* existing = find_info(canonical);
        if (existing != nullptr && existing->loaded) {
            return;
        }
        plugin_info info{canonical, false, {}, {}};
        try {
            module_loader loader(canonical, registry_);
            for (const auto& [name, ver] : loader.modules()) {
                info.modules.push_back(describe(registry_.at(name, ver)));
            }
            info.loaded = true;
            loaders_.push_back(std::move(loader));
        } catch (const std::exception& e) {
            info.error = e.what();  // загрузчик уже откатил частичную регистрацию
        }
        if (existing != nullptr) {
            *existing = std::move(info);
        } else {
            plugins_.push_back(std::move(info));
        }
    }

    [[nodiscard]] const std::vector<plugin_info>& plugins() const {
        return plugins_;
    }

    // Порты и проперти — пробным экземпляром: create(), перечислить owned()
    // и выбросить. Опора на контракт жизненного цикла: конструктор лёгкий,
    // тяжёлое — в initialize (не зовётся). Бросивший конструктор — модуль
    // «сломан», палитра покажет причину.
    [[nodiscard]] static module_info describe(const module_factory_base& factory) {
        module_info info{std::string(factory.name()), factory.get_version(), {}, {}, {}, false, {}};
        try {
            const module_ptr probe = factory.create();
            for (io::input_base* p : probe->inputs().owned()) {
                info.inputs.push_back({p->name(), p->type()});
            }
            for (io::output_base* p : probe->outputs().owned()) {
                info.outputs.push_back({p->name(), p->type()});
            }
            for (io::property_base* p : probe->properties().owned()) {
                info.properties.push_back({p->name(), p->kind(), p->default_string(), p->options(), p->persistent()});
            }
        } catch (const std::exception& e) {
            info.broken = true;
            info.error = e.what();
        }
        return info;
    }

   private:
    [[nodiscard]] static bool is_plugin_file(const std::filesystem::path& p) {
#if defined(_WIN32)
        return p.extension() == ".dll";
#else
        return p.extension() == ".so";
#endif
    }

    [[nodiscard]] plugin_info* find_info(const std::filesystem::path& canonical) {
        for (plugin_info& p : plugins_) {
            if (p.path == canonical) {
                return &p;
            }
        }
        return nullptr;
    }

    // Порядок членов = обратный порядок разрушения: загрузчики снимают
    // фабрики из реестра — реестр обязан пережить их.
    module_registry registry_;
    std::vector<module_loader> loaders_;
    std::vector<plugin_info> plugins_;
    std::vector<std::filesystem::path> search_dirs_;
};

}  // namespace atp::studio

#endif  // ATP_STUDIO_MODULE_MANAGER_HPP
