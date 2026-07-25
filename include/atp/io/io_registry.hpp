#ifndef ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP
#define ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/threading.hpp>

namespace atp::io::detail {

// Общая механика реестров входов, выходов и пропертей; владеет элементами.
// Наследник объявляет элементы ссылками:
//     input<int>& number = make<input<int>>("number");
// Хвост аргументов make уходит конструктору элемента как есть — за именем
// следует всё, что вид элемента о себе объявляет (safety у портов, дефолт и
// теги у пропертей).
// Некопируем: ссылки наследника привязаны к конкретным объектам в реестре.
// НЕ потокобезопасен — make/remove/get относятся к фазе настройки.
// kind — слово для сообщений об ошибках («input»/«output»); ожидается
// строковый литерал, поэтому хранится как string_view без владения.
template <std::derived_from<io_base> TBase>
class io_registry {
   public:
    io_registry(const io_registry&) = delete;
    io_registry& operator=(const io_registry&) = delete;

    // Хвост аргументов уходит конструктору элемента как есть: input попадает
    // на (name, safety), property — на (name, default, persistence, safety).
    template <std::derived_from<TBase> TItem, typename... TArgs>
    TItem& make(std::string name, TArgs&&... args) {
        auto item = std::make_unique<TItem>(name, std::forward<TArgs>(args)...);
        TItem& ref = *item;
        // try_emplace без аргументов: при дубликате ничего не конструируется
        // и item остаётся владельцем для текста ошибки.
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + ref.name() + "'");
        }
        it->second = {std::move(item), &ref};
        return ref;
    }

    // Невладеющая запись: публикация чужого порта под именем этого реестра.
    // Время жизни — контракт вызывающего: алиас живёт не дольше порта
    // (в группе-композите гарантируется структурно — она владеет детьми).
    template <std::derived_from<TBase> TItem>
    TItem& alias(std::string name, TItem& port) {
        auto [it, inserted] = registry_.try_emplace(std::move(name));
        if (!inserted) {
            throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + it->first + "'");
        }
        it->second = {nullptr, &port};
        return port;
    }

    // Точное совпадение динамического типа, а не dynamic_cast: queued_input<T>
    // наследует input<T>, поэтому апкаст к базе прошёл бы и вернул вырожденный
    // view. typeid(base) сравнивает именно конкретный вид элемента.
    template <std::derived_from<TBase> TItem>
    [[nodiscard]] TItem& get(const std::string& name) {
        TBase& base = at(name);
        if (typeid(base) != typeid(TItem)) {
            throw std::runtime_error(std::string(kind_) + " '" + name + "' has a different type");
        }
        return static_cast<TItem&>(base);
    }

    // Type-erased доступ по имени — пара в духе std::map: at() бросает,
    // find() возвращает nullptr. const-метод отдаёт неконстантную ссылку:
    // запись хранит указатель, константность реестра не распространяется
    // на порты.
    [[nodiscard]] TBase& at(const std::string& name) const {
        TBase* item = find(name);
        if (!item) {
            throw std::runtime_error("no " + std::string(kind_) + " named '" + name + "'");
        }
        return *item;
    }

    [[nodiscard]] TBase* find(const std::string& name) const {
        auto it = registry_.find(name);
        return it == registry_.end() ? nullptr : it->second.port;
    }

    bool remove(const std::string& name) {
        return registry_.erase(name) > 0;
    }

    // Перечисление элементов — для будущей машинерии соединений.
    [[nodiscard]] std::vector<const TBase*> list() const {
        std::vector<const TBase*> result;
        result.reserve(registry_.size());
        for (const auto& [name, e] : registry_) {
            result.push_back(e.port);
        }
        return result;
    }

    // Только владеемые порты — материал карты «порт → поток» у раннера:
    // реестры групп содержат одни алиасы и выпадают из карты сами.
    [[nodiscard]] std::vector<TBase*> owned() const {
        std::vector<TBase*> result;
        for (const auto& [name, e] : registry_) {
            if (e.owned) {
                result.push_back(e.port);
            }
        }
        return result;
    }

   protected:
    explicit io_registry(std::string_view kind) : kind_(kind) {}
    // Перемещение разрешено (объявленный удалённый копи-конструктор подавлял
    // бы неявный move): map переезжает, сами порты остаются в куче — ссылки
    // наследника и созданные соединения валидны. Нужно узлу ports и
    // module(TPorts): узел коммутируется до модуля и переносится внутрь.
    // Move-присваивания нет — ссылки-члены наследников его запрещают;
    // источник после переноса не используется.
    io_registry(io_registry&&) = default;
    ~io_registry() = default;  // защищённый: разрушение только через наследника

   private:
    // Запись различает владение: у владеемой owned держит объект, у алиаса
    // owned пуст — реестр публикует чужой порт (группа-композит показывает
    // порты детей). port валиден всегда.
    struct entry {
        std::unique_ptr<TBase> owned;
        TBase* port = nullptr;
    };

    std::string_view kind_;
    std::unordered_map<std::string, entry> registry_;
};

}  // namespace atp::io::detail

#endif  // ANITOOLSPLATFORM_IO_IO_REGISTRY_HPP
