#ifndef ANITOOLSPLATFORM_VERSION_HPP
#define ANITOOLSPLATFORM_VERSION_HPP

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace atp {

// Версия модуля с переменным числом компонентов: 1.2, 1.2.3, 1.2.3.4.
// Тип структурный — пригоден как NTTP (см. module.hpp), поэтому вместо
// vector — массив фиксированной ёмкости, а члены публичны и без
// подчёркивания (требование структурного типа, как у safety::locking).
// Фактическая длина — в count; хвост parts дополнен нулями, поэтому
// 1.2 == 1.2.0 — count влияет только на представление, не на сравнение.
struct version {
    // major.minor.patch.build — как у версий Windows; при необходимости
    // ёмкость поднимается одной константой.
    static constexpr std::size_t max_parts = 4;

    std::array<unsigned, max_parts> parts{};
    std::size_t count{};

    constexpr version() = default;

    template <std::convertible_to<unsigned>... TParts>
        requires(sizeof...(TParts) <= max_parts)
    constexpr version(TParts... p) : parts{static_cast<unsigned>(p)...}, count{sizeof...(TParts)} {}

    // Лексикографически по дополненному массиву; count не участвует.
    friend constexpr std::strong_ordering operator<=>(const version& left, const version& right) {
        return left.parts <=> right.parts;
    }
    friend constexpr bool operator==(const version& left, const version& right) {
        return left.parts == right.parts;
    }

    // Текстовое представление с фактическим числом компонентов: "1.2.3".
    // Версия без компонентов (version{}) печатается как "0" — пустая
    // строка в логах выглядела бы как ошибка форматирования.
    [[nodiscard]] std::string to_string() const {
        if (count == 0) {
            return "0";
        }
        std::string text = std::to_string(parts[0]);
        for (std::size_t i = 1; i < count; ++i) {
            text += '.';
            text += std::to_string(parts[i]);
        }
        return text;
    }
};

// Версия модуля, не объявившего свою (см. module_base::get_version).
inline constexpr version default_version{0, 0, 1};

namespace detail {

// Строковый литерал как NTTP: const char* в шаблон не передаётся,
// нужен структурный тип с копией символов (включая завершающий ноль).
template <std::size_t N>
struct fixed_string {
    std::array<char, N> chars{};

    constexpr fixed_string(const char (&text)[N]) {
        std::ranges::copy(text, chars.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const {
        return {chars.data(), N - 1};
    }
};

// Разбор "1.2.3". Любая ошибка формата — throw: разбор работает и в
// рантайме (см. try_parse_version), а в constant-evaluation (ver<"...">)
// бросок остаётся ошибкой компиляции — компилятор цитирует строку с
// текстом сообщения.
constexpr version parse_version(std::string_view text) {
    version result;
    unsigned part = 0;
    bool has_digit = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            const auto digit = static_cast<unsigned>(c - '0');
            if (part > (std::numeric_limits<unsigned>::max() - digit) / 10) {
                throw std::invalid_argument("version part overflows unsigned");
            }
            part = part * 10 + digit;
            has_digit = true;
        } else if (c == '.') {
            if (!has_digit) {
                throw std::invalid_argument("empty version part");
            }
            if (result.count == version::max_parts) {
                throw std::invalid_argument("too many version parts");
            }
            result.parts[result.count++] = part;
            part = 0;
            has_digit = false;
        } else {
            throw std::invalid_argument("invalid character in version");
        }
    }
    if (!has_digit) {
        throw std::invalid_argument("empty version part");
    }
    if (result.count == version::max_parts) {
        throw std::invalid_argument("too many version parts");
    }
    result.parts[result.count++] = part;
    return result;
}

}  // namespace detail

// Сахар объявления версии строкой: atp::ver<"1.2.3"> — то же значение,
// что atp::version{1, 2, 3}, но формат проверяется на компиляции.
template <detail::fixed_string Text>
inline constexpr version ver = detail::parse_version(Text.view());

// Рантайм-пара к ver<"...">: та же грамматика, но вместо исключения —
// nullopt: вызывающий (валидатор конфига) сам решает, как сообщить об ошибке.
[[nodiscard]] inline std::optional<version> try_parse_version(std::string_view text) noexcept {
    try {
        return detail::parse_version(text);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace atp

#endif  // ANITOOLSPLATFORM_VERSION_HPP
