#ifndef ANITOOLSPLATFORM_IO_HPP
#define ANITOOLSPLATFORM_IO_HPP

#include <concepts>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace atp::io {

    struct input_info {
        input_info(std::string name, std::type_index type)
            : name(std::move(name)), type(type), type_hash(type.hash_code()) {}

        std::string name;
        std::type_index type;   // typeid(std::tuple<Args...>) — источник истины
        std::size_t type_hash;  // производное, для отображения/быстрого сравнения
    };

    // Type-erased база: именно указатели на неё хранит реестр inputs.
    class input_base {
    public:
        input_base(std::string name, std::type_index type)
            : info_(std::move(name), type) {}
        virtual ~input_base() = default;

        input_base(const input_base&) = delete;
        input_base& operator=(const input_base&) = delete;

        [[nodiscard]] const input_info& info() const { return info_; }

        virtual void reset() = 0;

    private:
        input_info info_;
    };

    template <typename... Args>
    class basic_input : public input_base {
    public:
        explicit basic_input(std::string name)
            : input_base(std::move(name), typeid(std::tuple<Args...>)) {}

        // Шаблонные параметры вызова — настоящий perfect forwarding,
        // принимает и lvalue, и rvalue.
        template <typename... CallArgs>
            requires std::constructible_from<std::tuple<Args...>, CallArgs...>
        void operator()(CallArgs&&... call_args) {
            value_.emplace(std::forward<CallArgs>(call_args)...);
            if (callback_) {
                // Копия: колбэк может реентерабельно перезаписать value_ —
                // ссылки в сохранённый tuple были бы повисшими.
                auto snapshot = *value_;
                std::apply(callback_, snapshot);
            }
        }

        [[nodiscard]] bool has_value() const { return value_.has_value(); }

        [[nodiscard]] const std::tuple<Args...>& get() const {
            if (!value_) {
                throw std::runtime_error("input '" + info().name + "' has no value");
            }
            return *value_;
        }

        void reset() override { value_.reset(); }

        void when(std::function<void(const Args&...)> callback) {
            callback_ = std::move(callback);
        }

    private:
        std::optional<std::tuple<Args...>> value_;
        std::function<void(const Args&...)> callback_;
    };

    // Реестр входов. Хранит невладеющие указатели на поля-члены наследника,
    // поэтому некопируем и неперемещаем: адреса полей фиксированы.
    class inputs {
    public:
        template <typename... Args>
        struct input : basic_input<Args...> {
            input(inputs& parent, std::string name)
                : basic_input<Args...>(std::move(name)), parent_(parent) {
                parent_.add_input(*this);
            }
            ~input() override { parent_.remove_input(this->info().name); }

        private:
            inputs& parent_;
        };

        inputs() = default;
        inputs(const inputs&) = delete;
        inputs& operator=(const inputs&) = delete;

        [[nodiscard]] const input_info& get_input_info(const std::string& name) const {
            return find(name).info();
        }

        template <typename... Args>
        [[nodiscard]] basic_input<Args...>& get_input(const std::string& name) {
            input_base& base = find(name);
            if (base.info().type != std::type_index(typeid(std::tuple<Args...>))) {
                throw std::runtime_error("input '" + name + "' has a different type");
            }
            return static_cast<basic_input<Args...>&>(base);
        }

        // Перечисление входов — для будущей машинерии соединений.
        [[nodiscard]] std::vector<const input_info*> list() const {
            std::vector<const input_info*> result;
            result.reserve(registry_.size());
            for (const auto& [name, in] : registry_) {
                result.push_back(&in->info());
            }
            return result;
        }

    private:
        void add_input(input_base& in) {
            auto [it, inserted] = registry_.emplace(in.info().name, &in);
            if (!inserted) {
                throw std::runtime_error("duplicate input name '" + in.info().name + "'");
            }
        }

        void remove_input(const std::string& name) {
            registry_.erase(name);
        }

        [[nodiscard]] input_base& find(const std::string& name) const {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                throw std::runtime_error("no input named '" + name + "'");
            }
            return *it->second;
        }

        std::unordered_map<std::string, input_base*> registry_;
    };

    struct outputs {
        // Заглушка: дизайн outputs делается вместе с системой соединений.
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_HPP
