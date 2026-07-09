#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "input_base.hpp"
#include "threading.hpp"

namespace atp::io {

    // Реестр входов; владеет ими. Наследник объявляет входы ссылками:
    //     input<int>& number = make<input<int>>("number");
    //     input<int>& fast = make<input<int>>("fast", unsafe);
    //     queued_input<int>& events = make<queued_input<int>>("events");
    // Некопируем: ссылки наследника привязаны к конкретным объектам в реестре.
    // Сам реестр НЕ потокобезопасен — make/remove/get относятся к фазе
    // настройки. Потокобезопасность входа выбирается в точке make: по умолчанию
    // включена, тег unsafe выключает (см. safety).
    class inputs {
    public:
        inputs() = default;
        inputs(const inputs&) = delete;
        inputs& operator=(const inputs&) = delete;

        template <std::derived_from<input_base> TInput>
        TInput& make(std::string name, safety s = safe) {
            auto in = std::make_unique<TInput>(name, s);
            TInput& ref = *in;
            // try_emplace: при дубликате аргументы не переносятся — in остаётся владельцем.
            auto [it, inserted] = registry_.try_emplace(std::move(name), std::move(in));
            if (!inserted) {
                throw std::runtime_error("duplicate input name '" + ref.name() + "'");
            }
            return ref;
        }

        // Точное совпадение динамического типа, а не dynamic_cast: queued_input<T>
        // наследует input<T>, поэтому апкаст к базе прошёл бы и вернул вырожденный
        // view. typeid(base) сравнивает именно конкретный вид входа.
        template <std::derived_from<input_base> TInput>
        [[nodiscard]] TInput& get(const std::string& name) {
            input_base& base = find(name);
            if (typeid(base) != typeid(TInput)) {
                throw std::runtime_error("input '" + name + "' has a different type");
            }
            return static_cast<TInput&>(base);
        }

        bool remove(const std::string& name) {
            return registry_.erase(name) > 0;
        }

        // Нетипизированный доступ — для перечисления и сброса.
        [[nodiscard]] input_base& get_input(const std::string& name) {
            return find(name);
        }

        [[nodiscard]] const input_base& get_input(const std::string& name) const {
            return find(name);
        }

        // Перечисление входов — для будущей машинерии соединений.
        [[nodiscard]] std::vector<const input_base*> list() const {
            std::vector<const input_base*> result;
            result.reserve(registry_.size());
            for (const auto& [name, in] : registry_) {
                result.push_back(in.get());
            }
            return result;
        }

    private:
        [[nodiscard]] input_base& find(const std::string& name) const {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                throw std::runtime_error("no input named '" + name + "'");
            }
            return *it->second;
        }

        std::unordered_map<std::string, std::unique_ptr<input_base>> registry_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUTS_HPP
