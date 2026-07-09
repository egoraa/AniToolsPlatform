#include <iostream>
#include <string>

#include "platform/module.hpp"

namespace {

    struct source_outputs : atp::io::outputs {
        atp::io::output<int>& number = make<atp::io::output<int>>("number");
    };

    struct sink_inputs : atp::io::inputs {
        atp::io::input<int>& number = make<atp::io::input<int>>("number");
        atp::io::queued_input<int>& history = make<atp::io::queued_input<int>>("history");
    };

    class source_module : public atp::module<atp::io::inputs, source_outputs> {};

    class sink_module : public atp::module<sink_inputs, atp::io::outputs> {
    public:
        void initialize() override {
            inputs().number.when([](const int& value) {
                std::cout << "sink received: " << value << '\n';
            });
        }
    };

} // namespace

int main() {
    source_module source;
    sink_module sink;
    source.initialize();
    sink.initialize();

    // Соединение выход→вход: типизированное — прямо в коде,
    // type-erased по именам — путь будущей машинерии соединений.
    source.outputs().number.connect(sink.inputs().number);
    source.outputs().at("number").connect(sink.inputs().at("history"));

    source.outputs().number(42);  // рассылка обоим входам + кэш

    std::cout << "cached: " << source.outputs().number.get()
              << ", queued: " << sink.inputs().history.pop() << '\n';

    std::cout << "declared outputs:\n";
    for (const auto* info : source.outputs().list()) {
        std::cout << "  '" << info->name() << "' (type hash " << info->type().hash_code() << ")\n";
    }
    return 0;
}
