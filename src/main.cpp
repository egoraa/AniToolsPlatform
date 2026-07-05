#include <iostream>
#include <string>

#include "platform/module.hpp"

namespace {

    struct demo_inputs : atp::io::inputs {
        atp::io::inputs::input<int> number{*this, "number"};
        atp::io::inputs::input<std::string> text{*this, "text"};
    };

    class DemoModule : public atp::Module<demo_inputs, atp::io::outputs> {
    public:
        void initialize() override {
            inputs().number.when([](const int& value) {
                std::cout << "number received: " << value << '\n';
            });
            inputs().text.when([](const std::string& value) {
                std::cout << "text received: " << value << '\n';
            });
        }
    };

} // namespace

int main() {
    DemoModule module;
    module.initialize();

    module.inputs().number(42);
    module.inputs().text("Hello, AniTools!");

    std::cout << "declared inputs:\n";
    for (const auto* info : module.inputs().list()) {
        std::cout << "  '" << info->name << "' (type hash " << info->type_hash << ")\n";
    }
    return 0;
}
