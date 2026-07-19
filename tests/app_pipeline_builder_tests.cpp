#include <latch>
#include <stop_token>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atp/app/config_model.hpp>
#include <atp/app/config_validator.hpp>
#include <atp/app/pipeline_builder.hpp>
#include <atp/module.hpp>

namespace {

struct feed_outputs : atp::io::outputs {
    atp::io::output<int>& value = make<atp::io::output<int>>("value");
};
struct drain_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};

// Источник шлёт одно значение — тесту хватает факта доставки.
class one_shot_source : public atp::module<atp::io::inputs, feed_outputs, "one_shot"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        if (sent_) {
            return atp::work_status::idle;
        }
        sent_ = true;
        outputs().value(42);
        return atp::work_status::busy;
    }

   private:
    bool sent_ = false;
};

// Приёмник с параметрами: сохраняет сырую строку и сигналит о доставке.
class recording_sink : public atp::module<drain_inputs, atp::io::outputs, "recorder"> {
   public:
    // Снимок параметров — прямо из конструктора: build создаёт модуль через
    // фабрику до всяких каскадов, тест проверяет доставку params без запуска.
    explicit recording_sink(atp::module_config config) : config_(std::move(config)) {
        last_params = config_.raw;
    }

    static inline std::latch* delivered = nullptr;  // тест ставит перед стартом
    static inline std::string last_params;          // фабрика создаёт модуль внутри build

    atp::work_status iterate(std::stop_token) override {
        if (inputs().value.try_pop()) {
            if (delivered) {
                delivered->count_down();
            }
            return atp::work_status::busy;
        }
        return atp::work_status::idle;
    }

   private:
    atp::module_config config_;
};

atp::app::config make_config(const char* text) {
    const nlohmann::json doc = nlohmann::json::parse(text);
    const auto errors = atp::app::validate(doc);
    EXPECT_TRUE(errors.empty());
    return atp::app::decode(doc);
}

TEST(PipelineBuilder, BuildsTreeParamsAndRuns) {
    const atp::app::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {
            "children": [
                {"group": "left", "children": [{"module": "one_shot", "name": "src"}],
                 "expose": {"outputs": {"out": "src.value"}}},
                {"group": "right", "children": [{"module": "recorder", "params": {"tag": "demo"}}],
                 "expose": {"inputs": {"in": "recorder.value"}}}
            ],
            "connections": [{"from": "left.out", "to": "right.in"}]
        },
        "threads": [{"name": "a", "mode": "on_demand"}, {"name": "b", "mode": "on_demand"}],
        "assign": {"left": "a", "right": "b"}
    })");

    atp::app::application app;
    app.registry.add<one_shot_source>();
    app.registry.add<recording_sink>();
    atp::app::build(app, cfg, ".");

    // структура: группы и дети на местах
    ASSERT_NE(app.pipe.root().find_group("left"), nullptr);
    ASSERT_NE(app.pipe.root().find_group("right"), nullptr);
    EXPECT_NE(app.pipe.root().find_group("right")->find_module("recorder"), nullptr);

    // params дошли до конструктора модуля через фабрику
    EXPECT_EQ(recording_sink::last_params, R"({"tag":"demo"})");

    // конфиг раннера валиден: пайплайн реально запускается и доставляет
    std::latch delivered(1);
    recording_sink::delivered = &delivered;
    app.runner.start(app.pipe);
    delivered.wait();
    app.runner.stop();
    recording_sink::delivered = nullptr;
}

TEST(PipelineBuilder, WrapsPlatformErrorsWithConfigContext) {
    const atp::app::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"children": [{"module": "no_such_module"}]}
    })");

    atp::app::application app;
    try {
        atp::app::build(app, cfg, ".");
        FAIL() << "expected config_error";
    } catch (const atp::app::config_error& e) {
        EXPECT_NE(std::string(e.what()).find("no_such_module"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("root"), std::string::npos);  // контекст: в какой группе
    }
}

// config некопируем (unique_ptr в child_node) — «ломаный» путь добавляется
// прямо в модель после decode.
TEST(PipelineBuilder, UnknownAssignPathIsConfigError) {
    atp::app::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"children": [{"group": "g", "children": []}]},
        "threads": [{"name": "t"}]
    })");
    cfg.assignments.emplace_back("ghost", "t");  // валидатор такое ловит; builder — вторая линия обороны

    atp::app::application app;
    EXPECT_THROW(atp::app::build(app, cfg, "."), atp::app::config_error);
}

// Путь studio: реестр живёт на уровне сессии, пайплайн пересоздаётся на
// каждый запуск — сборка не должна требовать application и загрузку плагинов.
TEST(PipelineBuilder, BuildsIntoPrefilledRegistry) {
    const atp::app::config cfg = make_config(R"({
        "version": "1.0",
        "pipeline": {"children": [{"module": "one_shot"}]},
        "threads": [{"name": "t"}]
    })");

    atp::module_registry registry;
    registry.add<one_shot_source>();
    atp::pipeline pipe;
    atp::pipeline_runner runner;
    atp::app::build_pipeline(pipe, runner, cfg, registry);

    EXPECT_NE(pipe.root().find_module("one_shot"), nullptr);
    runner.start(pipe);  // потоки и раскладка тоже собраны
    runner.stop();
}

}  // namespace
