#ifndef ATP_STUDIO_VALUE_FORMAT_HPP
#define ATP_STUDIO_VALUE_FORMAT_HPP

#include <any>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

namespace atp::studio {

// Текст для мониторинга: ходовые типы значений на связях. Неизвестный тип —
// nullopt: GUI покажет имя типа вместо значения, вранья в цифрах не будет.
[[nodiscard]] inline std::optional<std::string> format_value(const std::any& value) {
    if (const int* v = std::any_cast<int>(&value)) {
        return std::to_string(*v);
    }
    if (const unsigned* v = std::any_cast<unsigned>(&value)) {
        return std::to_string(*v);
    }
    if (const std::int64_t* v = std::any_cast<std::int64_t>(&value)) {
        return std::to_string(*v);
    }
    if (const std::uint64_t* v = std::any_cast<std::uint64_t>(&value)) {
        return std::to_string(*v);
    }
    if (const float* v = std::any_cast<float>(&value)) {
        std::ostringstream out;  // ostringstream: без хвостовых нулей to_string
        out << *v;
        return out.str();
    }
    if (const double* v = std::any_cast<double>(&value)) {
        std::ostringstream out;
        out << *v;
        return out.str();
    }
    if (const bool* v = std::any_cast<bool>(&value)) {
        return *v ? "true" : "false";
    }
    if (const std::string* v = std::any_cast<std::string>(&value)) {
        return *v;
    }
    return std::nullopt;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_VALUE_FORMAT_HPP
