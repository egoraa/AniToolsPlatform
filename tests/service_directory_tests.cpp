#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/service_directory.hpp>

namespace {

// Чистые интерфейсы — единственное, что разделяют поставщик и потребитель.
// Защищённый деструктор: разрушение — забота владельца, не справочника.
struct greeter {
    virtual std::string greet() const = 0;

   protected:
    ~greeter() = default;
};

struct counter {
    virtual int next() = 0;

   protected:
    ~counter() = default;
};

struct greeter_impl : greeter {
    std::string greet() const override {
        return "hello";
    }
};

struct both_impl : greeter, counter {
    std::string greet() const override {
        return "hi";
    }
    int next() override {
        return ++value_;
    }

   private:
    int value_ = 0;
};

// Поставщик: публикует себя как интерфейс в initialize, снимает в stop.
class greeter_module : public atp::module<atp::io::inputs, atp::io::outputs, "greeter">, public greeter {
   public:
    void initialize(atp::module_context& context) override {
        context.services.provide<greeter>(std::string{get_name()}, *this);
    }
    void stop(atp::module_context& context) override {
        context.services.remove(std::string{get_name()});
    }
    std::string greet() const override {
        return "hello from module";
    }
};

// Потребитель: ищет соседа только в start — порядок initialize не важен.
class consumer_module : public atp::module<atp::io::inputs, atp::io::outputs, "consumer"> {
   public:
    void start(atp::module_context& context) override {
        greeting = context.services.at<greeter>("greeter").greet();
    }
    std::string greeting;
};

}  // namespace

TEST(ServiceDirectory, ProvideThenAtReturnsSameObject) {
    atp::service_directory directory;
    greeter_impl impl;
    directory.provide<greeter>("hello", impl);
    EXPECT_EQ(&directory.at<greeter>("hello"), static_cast<greeter*>(&impl));
    EXPECT_EQ(directory.at<greeter>("hello").greet(), "hello");
}

TEST(ServiceDirectory, FindReturnsNullptrForUnknownName) {
    atp::service_directory directory;
    EXPECT_EQ(directory.find<greeter>("missing"), nullptr);
}

TEST(ServiceDirectory, FindReturnsNullptrForUnknownInterface) {
    atp::service_directory directory;
    greeter_impl impl;
    directory.provide<greeter>("hello", impl);
    // имя есть, но под другим интерфейсом ничего не публиковалось
    EXPECT_EQ(directory.find<counter>("hello"), nullptr);
}

TEST(ServiceDirectory, AtThrowsOnUnknownName) {
    atp::service_directory directory;
    EXPECT_THROW((void)directory.at<greeter>("missing"), std::runtime_error);
}

TEST(ServiceDirectory, AtThrowsOnUnknownInterface) {
    atp::service_directory directory;
    greeter_impl impl;
    directory.provide<greeter>("hello", impl);
    EXPECT_THROW((void)directory.at<counter>("hello"), std::runtime_error);
}

TEST(ServiceDirectory, EmptyNameThrows) {
    atp::service_directory directory;
    greeter_impl impl;
    EXPECT_THROW(directory.provide<greeter>("", impl), std::invalid_argument);
}

TEST(ServiceDirectory, DuplicateProvideThrows) {
    atp::service_directory directory;
    greeter_impl first;
    greeter_impl second;
    directory.provide<greeter>("hello", first);
    EXPECT_THROW(directory.provide<greeter>("hello", second), std::runtime_error);
}

TEST(ServiceDirectory, SameNameDifferentInterfacesCoexist) {
    atp::service_directory directory;
    both_impl impl;
    directory.provide<greeter>("both", impl);
    directory.provide<counter>("both", impl);
    EXPECT_EQ(directory.at<greeter>("both").greet(), "hi");
    EXPECT_EQ(directory.at<counter>("both").next(), 1);
}

TEST(ServiceDirectory, SameInterfaceDifferentNamesCoexist) {
    atp::service_directory directory;
    greeter_impl impl;
    directory.provide<greeter>("first", impl);
    directory.provide<greeter>("second", impl);
    EXPECT_EQ(directory.find<greeter>("first"), directory.find<greeter>("second"));
}

TEST(ServiceDirectory, RemoveByNameDropsAllInterfaces) {
    atp::service_directory directory;
    both_impl impl;
    directory.provide<greeter>("both", impl);
    directory.provide<counter>("both", impl);
    EXPECT_TRUE(directory.remove("both"));
    EXPECT_EQ(directory.find<greeter>("both"), nullptr);
    EXPECT_EQ(directory.find<counter>("both"), nullptr);
}

TEST(ServiceDirectory, RemoveSingleInterfaceKeepsOthers) {
    atp::service_directory directory;
    both_impl impl;
    directory.provide<greeter>("both", impl);
    directory.provide<counter>("both", impl);
    EXPECT_TRUE(directory.remove<greeter>("both"));
    EXPECT_EQ(directory.find<greeter>("both"), nullptr);
    EXPECT_NE(directory.find<counter>("both"), nullptr);
}

TEST(ServiceDirectory, RemoveReturnsFalseWhenMissing) {
    atp::service_directory directory;
    greeter_impl impl;
    directory.provide<greeter>("hello", impl);
    EXPECT_FALSE(directory.remove("missing"));
    EXPECT_FALSE(directory.remove<counter>("hello"));
    // сама запись при этом не пострадала
    EXPECT_NE(directory.find<greeter>("hello"), nullptr);
}

TEST(ServiceDirectory, ModulePublishesInInitializeConsumerFindsInStart) {
    atp::service_directory services;
    atp::module_context context{services};
    greeter_module provider;
    consumer_module consumer;
    // потребитель инициализируется раньше поставщика — двухфазность
    // publish/lookup делает порядок initialize неважным
    consumer.initialize(context);
    provider.initialize(context);
    consumer.start(context);
    provider.start(context);
    EXPECT_EQ(consumer.greeting, "hello from module");

    // симметричное снятие публикаций в stop
    provider.stop(context);
    EXPECT_EQ(services.find<greeter>("greeter"), nullptr);
}
