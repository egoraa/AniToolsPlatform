#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "input.hpp"
#include "input_base.hpp"
#include "queued_input.hpp"
#include "threading.hpp"

namespace atp::io {

    // Реестр входов; владеет ими. Наследник объявляет входы ссылками:
    //     input<int>& number = make<int>("number");
    //     unsafe_input<int>& fast = make<int>("fast", unsafe);
    //     queued_input<int>& events = make_queued<int>("events");
    // Некопируем: ссылки наследника привязаны к конкретным объектам в реестре.
    // Сам реестр НЕ потокобезопасен — make/remove/get_* относятся к фазе
    // настройки. Потокобезопасность входа выбирается в точке make: по умолчанию
    // включена, тег unsafe выключает (null_mutex, ноль накладных расходов).
    class inputs {
    public:
        inputs() = default;
        inputs(const inputs&) = delete;
        inputs& operator=(const inputs&) = delete;

        template <typename... Args>
        input<Args...>& make(std::string name) {
            return emplace_input<input<Args...>>(std::move(name));
        }

        template <typename... Args>
        unsafe_input<Args...>& make(std::string name, unsafe_t) {
            return emplace_input<unsafe_input<Args...>>(std::move(name));
        }

        template <typename... Args>
        queued_input<Args...>& make_queued(std::string name) {
            return emplace_input<queued_input<Args...>>(std::move(name));
        }

        template <typename... Args>
        unsafe_queued_input<Args...>& make_queued(std::string name, unsafe_t) {
            return emplace_input<unsafe_queued_input<Args...>>(std::move(name));
        }

        bool remove(const std::string& name) {
            return registry_.erase(name) > 0;
        }

        // Неконстантная перегрузка обязательна: без неё вызов на неконстантном
        // реестре перехватывает шаблонный get_input с выведенным пустым пакетом.
        [[nodiscard]] input_base& get_input(const std::string& name) {
            return find(name);
        }

        [[nodiscard]] const input_base& get_input(const std::string& name) const {
            return find(name);
        }

        template <typename... Args>
        [[nodiscard]] input<Args...>& get_input(const std::string& name) {
            return get_typed<input<Args...>>(name);
        }

        template <typename... Args>
        [[nodiscard]] unsafe_input<Args...>& get_input(const std::string& name, unsafe_t) {
            return get_typed<unsafe_input<Args...>>(name);
        }

        template <typename... Args>
        [[nodiscard]] queued_input<Args...>& get_queued(const std::string& name) {
            return get_typed<queued_input<Args...>>(name);
        }

        template <typename... Args>
        [[nodiscard]] unsafe_queued_input<Args...>& get_queued(const std::string& name, unsafe_t) {
            return get_typed<unsafe_queued_input<Args...>>(name);
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
        template <typename TInput>
        TInput& emplace_input(std::string name) {
            auto in = std::make_unique<TInput>(name);
            TInput& ref = *in;
            // try_emplace: при дубликате аргументы не переносятся — in остаётся владельцем.
            auto [it, inserted] = registry_.try_emplace(std::move(name), std::move(in));
            if (!inserted) {
                throw std::runtime_error("duplicate input name '" + ref.name() + "'");
            }
            return ref;
        }

        // dynamic_cast, а не сравнение type(): сигнатура tuple<Args...> общая
        // у input и queued_input, различить конкретный вид может только RTTI.
        template <typename TInput>
        [[nodiscard]] TInput& get_typed(const std::string& name) {
            auto* typed = dynamic_cast<TInput*>(&find(name));
            if (typed == nullptr) {
                throw std::runtime_error("input '" + name + "' has a different type");
            }
            return *typed;
        }

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
