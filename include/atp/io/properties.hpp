#ifndef ANITOOLSPLATFORM_IO_PROPERTIES_HPP
#define ANITOOLSPLATFORM_IO_PROPERTIES_HPP

#include <atp/io/io_registry.hpp>
#include <atp/io/property_base.hpp>

namespace atp::io {

// Реестр пропертей; владеет ими. Зеркало inputs/outputs — та же механика
// и тот же паттерн объявления секции, дефолт и теги уходят конструктору:
//     property<int>& limit = make<property<int>>("limit", 10);
//     property<std::string>& file = make<property<std::string>>("file", "", transient);
//     property<int>& channels = make<property<int>>("channels", 2, allowed(1, 2, 6));
class properties : public detail::io_registry<property_base> {
   public:
    properties() : io_registry("property") {}
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTIES_HPP
