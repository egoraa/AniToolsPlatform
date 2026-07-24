#ifndef ANITOOLSPLATFORM_IO_PORTS_HPP
#define ANITOOLSPLATFORM_IO_PORTS_HPP

#include <concepts>

#include <atp/io/inputs.hpp>
#include <atp/io/outputs.hpp>

namespace atp::io {

// Узел портов модуля: пара секций «входы + выходы», передаётся в module<>
// одним параметром. Секции — обычные реестры-наследники inputs/outputs и
// объявляются по прежнему паттерну, узел лишь собирает их вместе:
//     struct my_in : inputs { input<int>& step = make<input<int>>("step"); };
//     struct my_out : outputs { output<int>& count = make<output<int>>("count"); };
//     using my_ports = ports<my_in, my_out>;
//     class my_module : public module<my_ports, "my"> { ... };
// Односторонним модулям вторая секция не нужна: ports<my_in> /
// ports<inputs, my_out>; ports<> — пустой узел (умолчание module<>).
// Узел перемещаем (вслед за реестрами): порты живут в куче, перенос не рвёт
// ни ссылки-члены секций, ни созданные соединения — узел можно скоммутировать
// до модуля и отдать в его конструктор. НЕ потокобезопасен — фаза настройки.
template <std::derived_from<inputs> TIn = inputs, std::derived_from<outputs> TOut = outputs>
struct ports {
    // Типы секций — для ковариантных inputs()/outputs() у module<>.
    using in_type = TIn;
    using out_type = TOut;

    TIn in;
    TOut out;
};

// Узел портов для module<>: сам ports или его наследник. Требование через
// in_type/out_type: подстановка сама отсеет типы без этих членов, а
// констрейнты ports — секции не от inputs/outputs.
template <typename T>
concept ports_node = std::derived_from<T, ports<typename T::in_type, typename T::out_type>>;

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PORTS_HPP
