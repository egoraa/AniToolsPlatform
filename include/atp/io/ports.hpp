#ifndef ANITOOLSPLATFORM_IO_PORTS_HPP
#define ANITOOLSPLATFORM_IO_PORTS_HPP

#include <concepts>

#include <atp/io/inputs.hpp>
#include <atp/io/outputs.hpp>
#include <atp/io/properties.hpp>

namespace atp::io {

// Узел портов модуля: три секции «входы + выходы + проперти», передаётся в
// module<> одним параметром. Секции — обычные реестры-наследники
// inputs/outputs/properties и объявляются по прежнему паттерну, узел лишь
// собирает их вместе:
//     struct my_in : inputs { input<int>& step = make<input<int>>("step"); };
//     struct my_out : outputs { output<int>& count = make<output<int>>("count"); };
//     struct my_props : properties { property<int>& limit = make<property<int>>("limit", 10); };
//     using my_ports = ports<my_in, my_out, my_props>;
//     class my_module : public module<my_ports, "my"> { ... };
// Ненужные секции опускаются справа: ports<my_in> / ports<inputs, my_out> /
// ports<my_in, my_out>; ports<> — пустой узел (умолчание module<>).
// Узел перемещаем (вслед за реестрами): порты живут в куче, перенос не рвёт
// ни ссылки-члены секций, ни созданные соединения — узел можно скоммутировать
// до модуля и отдать в его конструктор. НЕ потокобезопасен — фаза настройки.
template <std::derived_from<inputs> TIn = inputs,
          std::derived_from<outputs> TOut = outputs,
          std::derived_from<properties> TProps = properties>
struct ports {
    // Типы секций — для ковариантных inputs()/outputs()/properties() у module<>.
    using in_type = TIn;
    using out_type = TOut;
    using props_type = TProps;

    TIn in;
    TOut out;
    TProps props;
};

// Узел портов для module<>: сам ports или его наследник. Требование через
// in_type/out_type/props_type: подстановка сама отсеет типы без этих членов, а
// констрейнты ports — секции не от inputs/outputs/properties.
template <typename T>
concept ports_node = std::derived_from<T, ports<typename T::in_type, typename T::out_type, typename T::props_type>>;

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PORTS_HPP
