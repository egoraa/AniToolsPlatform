#ifndef ATP_APP_PIPELINE_BUILDER_HPP
#define ATP_APP_PIPELINE_BUILDER_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <atp/app/config_model.hpp>
#include <atp/group.hpp>
#include <atp/module_loader.hpp>
#include <atp/module_registry.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

namespace atp::app {

// Собранное приложение. Порядок членов = порядок разрушения в обратную
// сторону: раннер умирает первым (останавливает потоки, ссылаясь на
// пайплайн), пайплайн — раньше загрузчиков (модули пинят свои DLL сами),
// реестр — последним (загрузчики снимают из него свои фабрики).
struct application {
    module_registry registry;
    std::vector<module_loader> plugins;
    pipeline pipe;
    pipeline_runner runner;

    application() = default;
    application(const application&) = delete;
    application& operator=(const application&) = delete;
};

namespace detail {

// Ошибки платформы оборачиваются контекстом конфига: пользователь видит,
// какой узел собирался, а не только «no module named X».
inline void build_group(group& g, const group_node& node, module_registry& registry) {
    for (const child_node& c : node.children) {
        if (c.module) {
            try {
                module_ptr m = c.module->factory_version
                                   ? registry.create(c.module->factory, *c.module->factory_version, c.module->params)
                                   : registry.create(c.module->factory, c.module->params);
                g.add(c.module->name, std::move(m));
            } catch (const std::exception& e) {
                throw config_error("module '" + c.module->name + "' in group '" + std::string(g.get_name()) +
                                   "': " + e.what());
            }
        } else {
            build_group(g.add_group(c.group->name), *c.group, registry);
        }
    }
    // Экспорты после детей, соединения после экспортов: пути соединений
    // могут ссылаться на алиасы подгрупп, у тех к этому моменту всё готово.
    for (const auto& [alias, path] : node.expose_inputs) {
        try {
            g.expose_input(alias, path);
        } catch (const std::exception& e) {
            throw config_error("expose input '" + alias + "' in group '" + std::string(g.get_name()) +
                               "': " + e.what());
        }
    }
    for (const auto& [alias, path] : node.expose_outputs) {
        try {
            g.expose_output(alias, path);
        } catch (const std::exception& e) {
            throw config_error("expose output '" + alias + "' in group '" + std::string(g.get_name()) +
                               "': " + e.what());
        }
    }
    for (const connection_node& c : node.connections) {
        try {
            if (c.replay) {
                g.connect(c.from, c.to, io::replay);
            } else {
                g.connect(c.from, c.to);
            }
        } catch (const std::exception& e) {
            throw config_error("connection '" + c.from + "' -> '" + c.to + "' in group '" +
                               std::string(g.get_name()) + "': " + e.what());
        }
    }
}

inline group& resolve_group(group& root, const std::string& path) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        group* next = current->find_group(segment);
        if (!next) {
            throw config_error("assign: group path '" + path + "' not found in pipeline");
        }
        current = next;
        if (dot == std::string::npos) {
            return *current;
        }
        begin = dot + 1;
    }
}

}  // namespace detail

// Сборка по модели: плагины → дерево групп и модулей → потоки → раскладка.
// Раннер НЕ запускается — start/остановка остаются за вызывающим (main).
// base_dir — каталог корневого конфига: пути плагинов считаются от него.
inline void build(application& app, const config& cfg, const std::filesystem::path& base_dir) {
    for (const std::string& p : cfg.plugins) {
        app.plugins.emplace_back(base_dir / p, app.registry);  // сбой загрузки — runtime_error с путём
    }
    detail::build_group(app.pipe.root(), cfg.pipeline, app.registry);
    for (const thread_node& t : cfg.threads) {
        app.runner.add_thread(t.name, {t.mode, t.period});
    }
    for (const auto& [path, thread] : cfg.assignments) {
        app.runner.assign(detail::resolve_group(app.pipe.root(), path), thread);
    }
}

}  // namespace atp::app

#endif  // ATP_APP_PIPELINE_BUILDER_HPP
