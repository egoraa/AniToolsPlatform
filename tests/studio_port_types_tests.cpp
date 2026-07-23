#include <any>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>

#include <gtest/gtest.h>

#include <atp/app/config_model.hpp>
#include <atp/studio/port_types.hpp>

namespace {

// Описатель без реестра и DLL: проверяем политику связывания, а не загрузку
// плагинов (её покрывают тесты module_manager).
class fake_catalog {
   public:
    void add(const std::string& factory, atp::studio::module_info info) {
        info.name = factory;
        modules_.emplace(factory, std::move(info));
    }

    [[nodiscard]] atp::studio::describe_fn describer() const {
        return
            [this](const std::string& factory, const std::optional<atp::version>&) -> const atp::studio::module_info* {
                auto it = modules_.find(factory);
                return it == modules_.end() ? nullptr : &it->second;
            };
    }

   private:
    std::map<std::string, atp::studio::module_info> modules_;
};

atp::studio::port_info port(const std::string& name, std::type_index type) {
    return {name, type};
}

// Документ: source (out int) → sink (in int) → text_sink (in string) →
// any_sink (in std::any).
atp::studio::document make_doc() {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_module("", "source", "source");
    doc.add_module("", "sink", "sink");
    doc.add_module("", "text_sink", "text_sink");
    doc.add_module("", "any_sink", "any_sink");
    return doc;
}

fake_catalog make_catalog() {
    fake_catalog catalog;
    atp::studio::module_info source;
    source.outputs.push_back(port("value", typeid(int)));
    catalog.add("source", source);

    atp::studio::module_info sink;
    sink.inputs.push_back(port("value", typeid(int)));
    catalog.add("sink", sink);

    atp::studio::module_info text_sink;
    text_sink.inputs.push_back(port("value", typeid(std::string)));
    catalog.add("text_sink", text_sink);

    atp::studio::module_info any_sink;
    any_sink.inputs.push_back(port("value", typeid(std::any)));
    catalog.add("any_sink", any_sink);
    return catalog;
}

TEST(PortTypes, ConnectAcceptsMatchingTypesAndAnyInput) {
    atp::studio::document doc = make_doc();
    const fake_catalog catalog = make_catalog();

    atp::studio::connect_ports(doc, "", "source.value", "sink.value", catalog.describer());
    // input<std::any> универсален — то же правило, что у io::input::accepts
    atp::studio::connect_ports(doc, "", "source.value", "any_sink.value", catalog.describer());
    EXPECT_EQ(doc.group_at("")->connections.size(), 2u);
}

TEST(PortTypes, ConnectRejectsMismatchedTypesWithoutTouchingDocument) {
    atp::studio::document doc = make_doc();
    const fake_catalog catalog = make_catalog();

    EXPECT_THROW(atp::studio::connect_ports(doc, "", "source.value", "text_sink.value", catalog.describer()),
                 atp::app::config_error);
    EXPECT_TRUE(doc.group_at("")->connections.empty());
}

// Незагруженный плагин — типы неизвестны: запрещать по незнанию хуже, чем
// дать рантайму отказать при запуске.
TEST(PortTypes, ConnectAllowsUnknownTypes) {
    atp::studio::document doc = make_doc();
    fake_catalog catalog;  // пустой каталог: describe вернёт nullptr

    atp::studio::connect_ports(doc, "", "source.value", "text_sink.value", catalog.describer());
    EXPECT_EQ(doc.group_at("")->connections.size(), 1u);
}

// Порт подгруппы виден только через expose — тип берётся у реального порта
// за псевдонимом.
TEST(PortTypes, ResolvesSubgroupPortsThroughExpose) {
    atp::studio::document doc = atp::studio::document::create();
    doc.add_group("", "inner");
    doc.add_module("inner", "source", "source");
    doc.set_expose_output("inner", "out", "source.value");
    doc.add_module("", "sink", "sink");
    doc.add_module("", "text_sink", "text_sink");
    const fake_catalog catalog = make_catalog();

    const auto type = atp::studio::resolve_port_type(*doc.group_at(""), "inner.out", true, catalog.describer());
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(*type, std::type_index(typeid(int)));

    EXPECT_THROW(atp::studio::connect_ports(doc, "", "inner.out", "text_sink.value", catalog.describer()),
                 atp::app::config_error);
    atp::studio::connect_ports(doc, "", "inner.out", "sink.value", catalog.describer());
    EXPECT_EQ(doc.group_at("")->connections.size(), 1u);
}

// expose_port: алиас из имени порта, дедуп суффиксом, идемпотентность.
TEST(PortTypes, ExposePortOutputUsesPortNameAlias) {
    atp::studio::document doc = make_doc();
    const std::string alias = atp::studio::expose_port(doc, "", "source.value", true);
    EXPECT_EQ(alias, "value");
    const auto& outs = doc.group_at("")->expose_outputs;
    ASSERT_EQ(outs.size(), 1u);
    EXPECT_EQ(outs[0].first, "value");
    EXPECT_EQ(outs[0].second, "source.value");
    EXPECT_TRUE(doc.group_at("")->expose_inputs.empty());
}

TEST(PortTypes, ExposePortInputGoesToInputMap) {
    atp::studio::document doc = make_doc();
    const std::string alias = atp::studio::expose_port(doc, "", "sink.value", false);
    EXPECT_EQ(alias, "value");
    const auto& ins = doc.group_at("")->expose_inputs;
    ASSERT_EQ(ins.size(), 1u);
    EXPECT_EQ(ins[0].second, "sink.value");
    EXPECT_TRUE(doc.group_at("")->expose_outputs.empty());
}

TEST(PortTypes, ExposePortDedupesAliasOnCollision) {
    atp::studio::document doc = make_doc();
    doc.add_module("", "source", "source2");  // фабрика source, выход "value"
    EXPECT_EQ(atp::studio::expose_port(doc, "", "source.value", true), "value");
    EXPECT_EQ(atp::studio::expose_port(doc, "", "source2.value", true), "value_2");
    EXPECT_EQ(doc.group_at("")->expose_outputs.size(), 2u);
}

TEST(PortTypes, ExposePortIsIdempotentForSamePort) {
    atp::studio::document doc = make_doc();
    EXPECT_EQ(atp::studio::expose_port(doc, "", "source.value", true), "value");
    EXPECT_EQ(atp::studio::expose_port(doc, "", "source.value", true), "value");
    EXPECT_EQ(doc.group_at("")->expose_outputs.size(), 1u);
}

}  // namespace
