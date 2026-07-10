#ifndef ANITOOLSPLATFORM_IO_REGISTRY_HPP
#define ANITOOLSPLATFORM_IO_REGISTRY_HPP

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

    // Общая механика реестров входов и выходов; владеет элементами.
    // Наследник объявляет элементы ссылками:
    //     input<int>& number = make<input<int>>("number");
    // Некопируем: ссылки наследника привязаны к конкретным объектам в реестре.
    // НЕ потокобезопасен — make/remove/get относятся к фазе настройки.
    // kind — слово для сообщений об ошибках («input»/«output»); ожидается
    // строковый литерал, поэтому хранится как string_view без владения.
    template <std::derived_from<io_base> TBase>
    class registry {
    public:
        registry(const registry&) = delete;
        registry& operator=(const registry&) = delete;

        template <std::derived_from<TBase> TItem>
        TItem& make(std::string name, safety s = safe) {
            auto item = std::make_unique<TItem>(name, s);
            TItem& ref = *item;
            // try_emplace: при дубликате аргументы не переносятся — item остаётся владельцем.
            auto [it, inserted] = registry_.try_emplace(std::move(name), std::move(item));
            if (!inserted) {
                throw std::runtime_error("duplicate " + std::string(kind_) + " name '" + ref.name() + "'");
            }
            return ref;
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
        // const unique_ptr разыменовывается в неконстантный объект, поэтому
        // одна функция обслуживает оба варианта.
        [[nodiscard]] TBase& at(const std::string& name) const {
            TBase* item = find(name);
            if (!item) {
                throw std::runtime_error("no " + std::string(kind_) + " named '" + name + "'");
            }
            return *item;
        }

        [[nodiscard]] TBase* find(const std::string& name) const {
            auto it = registry_.find(name);
            return it == registry_.end() ? nullptr : it->second.get();
        }

        bool remove(const std::string& name) {
            return registry_.erase(name) > 0;
        }

        // Перечисление элементов — для будущей машинерии соединений.
        [[nodiscard]] std::vector<const TBase*> list() const {
            std::vector<const TBase*> result;
            result.reserve(registry_.size());
            for (const auto& [name, item] : registry_) {
                result.push_back(item.get());
            }
            return result;
        }

    protected:
        explicit registry(std::string_view kind) : kind_(kind) {}
        ~registry() = default;  // защищённый: разрушение только через наследника

    private:
        std::string_view kind_;
        std::unordered_map<std::string, std::unique_ptr<TBase>> registry_;
    };

} // namespace atp::io::detail

#endif // ANITOOLSPLATFORM_IO_REGISTRY_HPP
