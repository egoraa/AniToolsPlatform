#ifndef ANITOOLSPLATFORM_GROUP_HPP
#define ANITOOLSPLATFORM_GROUP_HPP

#include <concepts>
#include <exception>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/module_registry.hpp>

namespace atp {

// Группа — модуль-композит: владеет детьми (модулями и подгруппами — те же
// модули), реализует жизненный цикл каскадами по ним и публикует избранные
// порты детей алиасами в собственных реестрах (см. expose_*, Task 6).
// НЕ единица исполнения: как группа исполняется (свой поток или inline у
// предка), решает раскладка раннера — вложенность здесь только инкапсуляция.
// НЕ потокобезопасна: состав и экспорт — фаза настройки. Каскады зовёт
// раннер; ручной root.initialize() в обход раннера — двойные каскады.
class group : public module_base {
   public:
    // Запись ребёнка: имя в области видимости группы (алиасы, анонимы) +
    // владение (module_ptr несёт пин DLL — модуль плагина держит свою
    // библиотеку). detached — ребёнок исполняется собственным потоком,
    // родительский iterate его пропускает; ставит и снимает раннер.
    struct child {
        std::string name;
        module_ptr module;
        bool detached = false;
    };

    explicit group(std::string name) : name_(std::move(name)) {}

    group(const group&) = delete;
    group& operator=(const group&) = delete;

    // Имя корня и диагностика; имена детей живут в записях родителя.
    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }

    // Приём готового модуля — в т.ч. созданного module_registry::create().
    module_base& add(std::string name, module_ptr module) {
        if (!module) {
            throw std::invalid_argument("null module for '" + name + "' in group '" + name_ + "'");
        }
        ensure_unique(name);
        children_.push_back({std::move(name), std::move(module), false});
        return *children_.back().module;
    }

    // Сахар: создать модуль на месте. Аргументы — конструктору модуля.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires std::constructible_from<M, TArgs...>
    M& make(std::string name, TArgs&&... args) {
        module_ptr module(new M(std::forward<TArgs>(args)...), {});  // монолит: пин пуст
        M& ref = static_cast<M&>(*module);
        add(std::move(name), std::move(module));
        return ref;
    }

    // Имя из самого модуля — тот же контракт has_module_name, что в реестре
    // фабрик. При коллизии перегрузок — указывать имя явно.
    template <std::derived_from<module_base> M, typename... TArgs>
        requires has_module_name<M> && std::constructible_from<M, TArgs...>
    M& make(TArgs&&... args) {
        return make<M>(std::string{M::module_name}, std::forward<TArgs>(args)...);
    }

    // Подгруппа — такой же ребёнок-модуль; имя дублируется в её конструктор
    // для get_name (диагностика).
    group& add_group(std::string name) {
        std::string ctor_name = name;
        return make<group>(std::move(name), std::move(ctor_name));
    }

    [[nodiscard]] const std::vector<child>& children() const {
        return children_;
    }

    [[nodiscard]] module_base* find_module(const std::string& name) const {
        const child* c = find_child(name);
        return c ? c->module.get() : nullptr;
    }

    [[nodiscard]] group* find_group(const std::string& name) const {
        return dynamic_cast<group*>(find_module(name));
    }

    // Служебный крючок раннера: подгруппа, назначенная своему потоку,
    // исключается из iterate родителя. Вне раннера не звать.
    void set_detached(const group& detached_child, bool value) {
        for (child& c : children_) {
            if (c.module.get() == &detached_child) {
                c.detached = value;
                return;
            }
        }
        throw std::invalid_argument("group '" + name_ + "' has no such child group");
    }

    // --- Экспорт портов: алиасы в собственных реестрах группы ---
    // Только путь «<дитя>.<порт>»: порт находится через модульный интерфейс
    // ребёнка — модуль и подгруппа неразличимы, ре-экспорт указывает сразу
    // на реальный порт. Перегрузки по ссылке нет намеренно: принадлежность
    // порта группе непроверяема, ошибка вызывающего молча искажала бы
    // валидацию межпоточности.

    void expose_input(std::string alias, const std::string& path) {
        inputs_.alias(std::move(alias), resolve_input(path));
    }

    void expose_output(std::string alias, const std::string& path) {
        outputs_.alias(std::move(alias), resolve_output(path));
    }

    // --- Соединения: пути в области видимости группы ---
    // Запись — только пара портов: владельцев здесь нет, карту
    // «порт → поток» для валидации раннер строит сам от владеемых
    // портов модулей.
    struct connection {
        io::output_base* out;
        io::input_base* in;
    };

    void connect(const std::string& from, const std::string& to) {
        link(from, to, false);
    }

    void connect(const std::string& from, const std::string& to, io::replay_t) {
        link(from, to, true);
    }

    [[nodiscard]] const std::vector<connection>& connections() const {
        return connections_;
    }

    // Тело деструктора выполняется до деструкторов членов: обе стороны
    // каждой записи ещё живы — «disconnect до разрушения входа» соблюдается
    // конструктивно.
    ~group() override {
        for (const connection& c : connections_) {
            (void)c.out->disconnect(*c.in);
        }
    }

    // Порты группы — алиасы на порты детей (наполняются expose_*).
    [[nodiscard]] io::inputs& inputs() override {
        return inputs_;
    }
    [[nodiscard]] const io::inputs& inputs() const override {
        return inputs_;
    }
    [[nodiscard]] io::outputs& outputs() override {
        return outputs_;
    }
    [[nodiscard]] const io::outputs& outputs() const override {
        return outputs_;
    }

    // Группа-композит собственных пропертей не имеет: реестр пуст.
    // Проперти детей достаются по путям (см. runtime::property_override),
    // алиасов на уровне группы нет — до появления нужды.
    [[nodiscard]] io::properties& properties() override {
        return properties_;
    }
    [[nodiscard]] const io::properties& properties() const override {
        return properties_;
    }

    // --- Жизненный цикл: каскады по порядку вставки ---

    // Локальный fail-fast: бросок ребёнка — stop уже инициализированным в
    // обратном порядке (ошибки отката глотаются — первопричина важнее),
    // затем переброс. Внешние группы откатывают своих предыдущих детей той
    // же логикой — рекурсивно.
    void initialize(module_context& context) override {
        std::size_t done = 0;
        try {
            for (child& c : children_) {
                c.module->initialize(context);
                ++done;
            }
        } catch (...) {
            for (std::size_t i = done; i > 0; --i) {
                try {
                    children_[i - 1].module->stop();
                } catch (...) {  // NOLINT(bugprone-empty-catch)
                }
            }
            throw;
        }
    }

    // Отката здесь нет: при ошибке раннер зовёт root.stop() — stop обязан
    // быть корректен после initialize без start (контракт module_base).
    void start() override {
        for (child& c : children_) {
            c.module->start();
        }
    }

    // Обратный порядок; при ошибке ребёнка — продолжить остальных, первую
    // ошибку перебросить в конце: остановка важнее её диагностики.
    void stop() override {
        std::exception_ptr first;
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            try {
                it->module->stop();
            } catch (...) {
                if (!first) {
                    first = std::current_exception();
                }
            }
        }
        if (first) {
            std::rethrow_exception(first);
        }
    }

    // Агрегация пасса: busy побеждает; detached-дети — у них свой поток.
    work_status iterate(std::stop_token token) override {
        work_status pass = work_status::idle;
        for (child& c : children_) {
            if (token.stop_requested()) {
                return pass;
            }
            if (c.detached) {
                continue;
            }
            if (c.module->iterate(token) == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

   private:
    [[nodiscard]] const child* find_child(const std::string& name) const {
        // линейный поиск: фаза настройки, составы групп невелики
        for (const child& c : children_) {
            if (c.name == name) {
                return &c;
            }
        }
        return nullptr;
    }

    void ensure_unique(const std::string& name) const {
        if (name.empty()) {
            throw std::invalid_argument("empty child name in group '" + name_ + "'");
        }
        if (find_child(name)) {
            throw std::runtime_error("duplicate name '" + name + "' in group '" + name_ + "'");
        }
    }

    // Путь «<дитя>.<порт>» в области видимости группы.
    [[nodiscard]] std::pair<module_base*, std::string> split_path(const std::string& path) const {
        auto dot = path.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == path.size()) {
            throw std::invalid_argument("path '" + path + "' in group '" + name_ + "': expected '<child>.<port>'");
        }
        module_base* child_module = find_module(path.substr(0, dot));
        if (!child_module) {
            throw std::runtime_error("group '" + name_ + "' has no child '" + path.substr(0, dot) + "'");
        }
        return {child_module, path.substr(dot + 1)};
    }

    [[nodiscard]] io::input_base& resolve_input(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::input_base* port = child_module->inputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no input '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }

    [[nodiscard]] io::output_base& resolve_output(const std::string& path) const {
        auto [child_module, port_name] = split_path(path);
        io::output_base* port = child_module->outputs().find(port_name);
        if (!port) {
            throw std::runtime_error("child '" + std::string(child_module->get_name()) + "' has no output '" +
                                     port_name + "' (path '" + path + "' in group '" + name_ + "')");
        }
        return *port;
    }

    void link(const std::string& from, const std::string& to, bool deliver_cached) {
        io::output_base& out = resolve_output(from);
        io::input_base& in = resolve_input(to);
        if (deliver_cached) {
            out.connect(in, io::replay);
        } else {
            out.connect(in);
        }
        connections_.push_back({&out, &in});
    }

    std::string name_;
    std::vector<child> children_;
    std::vector<connection> connections_;
    io::inputs inputs_;
    io::outputs outputs_;
    io::properties properties_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_GROUP_HPP
