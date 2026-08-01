#include <chrono>
#include <iostream>
#include <latch>
#include <stop_token>
#include <string>

#include <atp/module.hpp>
#include <atp/pipeline.hpp>
#include <atp/pipeline_runner.hpp>

namespace {

struct source_outputs : atp::io::outputs {
    atp::io::output<int>& number = make<atp::io::output<int>>("number");
};
using source_ports = atp::io::ports<atp::io::inputs, source_outputs>;

struct sink_inputs : atp::io::inputs {
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
    atp::io::queued_input<int>& history = make<atp::io::queued_input<int>>("history");
};
using sink_ports = atp::io::ports<sink_inputs>;

class source_module : public atp::module<source_ports, "source"> {
   public:
    atp::work_status iterate(std::stop_token) override {
        if (next_ > 3) {
            return atp::work_status::idle;
        }
        outputs().number(next_++);
        return atp::work_status::busy;
    }

   private:
    int next_ = 1;
};

class sink_module : public atp::module<sink_ports, "sink"> {
   public:
    std::latch* done = nullptr;

    void initialize(atp::module_context&) override {
        watcher_.watch(inputs().number, [this](const int& value) {
            std::cout << "sink received: " << value << '\n';
            if (value == 3 && done) {
                done->count_down();
            }
        });
    }
    atp::work_status iterate(std::stop_token) override {
        return watcher_.poll();
    }

   private:
    atp::io::watcher watcher_;
};

}  // namespace

int main() {
    atp::pipeline pipe;
    std::latch done(1);

    atp::group& producers = pipe.root().add_group("producers");
    producers.make<source_module>();
    producers.expose_output("numbers", "source.number");

    atp::group& consumers = pipe.root().add_group("consumers");
    sink_module& sink = consumers.make<sink_module>();
    sink.done = &done;
    consumers.expose_input("numbers", "sink.number");
    consumers.expose_input("log", "sink.history");

    pipe.root().connect("producers.numbers", "consumers.numbers");
    pipe.root().connect("producers.numbers", "consumers.log");

    atp::pipeline_runner runner;
    runner.add_thread("producing");
    runner.add_thread("consuming", {atp::thread_mode::throttled, std::chrono::milliseconds(5)});
    runner.assign(producers, "producing");
    runner.assign(consumers, "consuming");
    runner.start(pipe);
    done.wait();
    runner.stop();

    std::cout << "queued history:";
    while (!sink.inputs().history.empty()) {
        std::cout << ' ' << sink.inputs().history.pop();
    }
    std::cout << '\n';
    return 0;
}
