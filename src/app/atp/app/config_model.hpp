#ifndef ATP_APP_CONFIG_MODEL_HPP
#define ATP_APP_CONFIG_MODEL_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <atp/pipeline_runner.hpp>
#include <atp/version.hpp>

namespace atp::app {

// Версия схемы конфига, которую понимает приложение: мажор конфига обязан
// совпадать, минор — не превышать наш (поля «из будущего» отклоняются,
// а не игнорируются молча). Само поле "version" — первое, что проверяется.
inline constexpr version config_schema_version{1, 0};

// Ошибка уровня приложения: чтение, инклуды, сборка по конфигу.
class config_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

struct module_node {
    std::string factory;                     // имя фабрики в реестре
    std::string name;                        // имя ребёнка в группе (дефолт — имя фабрики)
    std::optional<version> factory_version;  // нет — последняя зарегистрированная
    std::string params;                      // сырой JSON узла params; "" — параметров нет
};

struct group_node;

// Ребёнок группы: заполнено ровно одно из полей (инвариант decode).
// unique_ptr — рекурсия типа; порядок в векторе значим — это порядок
// вставки в группу, то есть порядок каскадов жизненного цикла.
struct child_node {
    std::optional<module_node> module;
    std::unique_ptr<group_node> group;
};

struct connection_node {
    std::string from;  // пути "дитя.порт" в области видимости группы
    std::string to;
    bool replay = false;
};

struct group_node {
    std::string name;
    std::vector<child_node> children;
    std::vector<std::pair<std::string, std::string>> expose_inputs;  // алиас → "дитя.порт"
    std::vector<std::pair<std::string, std::string>> expose_outputs;
    std::vector<connection_node> connections;
};

struct thread_node {
    std::string name;
    thread_mode mode = thread_mode::on_demand;
    std::chrono::milliseconds period{};  // только для throttled
};

struct config {
    version schema;                    // поле "version" корневого документа
    std::vector<std::string> plugins;  // пути относительно каталога конфига
    group_node pipeline;               // корень; имя всегда "root"
    std::vector<thread_node> threads;
    std::vector<std::pair<std::string, std::string>> assignments;  // путь группы → имя потока
};

}  // namespace atp::app

#endif  // ATP_APP_CONFIG_MODEL_HPP
