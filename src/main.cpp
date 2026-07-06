#include <iostream>
#include <string>

#include "platform/module.hpp"

namespace {

    struct demo_inputs : atp::io::inputs {
        atp::io::input<int>& number = make<int>("number");
        atp::io::input<std::string>& text = make<std::string>("text");
        atp::io::input<int, double>& pair = make<int, double>("pair");
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
            inputs().pair.when([](const int first, double second) {
                std::cout << "pair received: " << first << ", " << second << '\n';
            });
        }
    };

} // namespace

int main() {
    DemoModule module;
    module.initialize();

    module.inputs().number(42);
    module.inputs().text("Hello, AniTools!");
    module.inputs().pair(1, 2.5);

    std::cout << "declared inputs:\n";
    for (const auto* info : module.inputs().list()) {
        std::cout << "  '" << info->name() << "' (type hash " << info->type().hash_code() << ")\n";
    }
    return 0;
}
