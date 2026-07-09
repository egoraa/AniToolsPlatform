#include <iostream>
#include <string>

#include "platform/module.hpp"

namespace {

    struct demo_inputs : atp::io::inputs {
        atp::io::input<int>& number = make<atp::io::input<int>>("number");
        atp::io::input<std::string>& text = make<atp::io::input<std::string>>("text");
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
        std::cout << "  '" << info->name() << "' (type hash " << info->type().hash_code() << ")\n";
    }
    return 0;
}
