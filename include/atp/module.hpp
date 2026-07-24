#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>
#include <string_view>
#include <utility>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

// «module» — контекстно-зависимое слово C++20: внутри namespace atp
// класс с таким именем легален и конфликтов не создаёт.
// Порты объявляются одним узлом io::ports<TIn, TOut> (пара секций) и
// передаются одним параметром; имя вторым, версия третьей: имя нужно чаще,
// а умолчания «через одно» в C++ не работают. Пустое имя — «аноним»: такой
// модуль регистрируется только под явным именем (module_registry::add<M>(name)).
template <io::ports_node TPorts = io::ports<>, detail::fixed_string Name = "", version Version = default_version>
class module : public module_base {
   public:
    // Имя и версия объявляются один раз, NTTP-параметрами, и доступны
    // и на компиляции (module_name/module_version), и в рантайме
    // (get_name/get_version) — хранить в объекте нечего. view указывает
    // в template parameter object — статическая длительность хранения.
    static constexpr std::string_view module_name = Name.view();
    static constexpr version module_version = Version;

    module() = default;
    // Узел можно скоммутировать до модуля и отдать при создании: порты
    // живут в куче, перенос узла не рвёт ни ссылки-члены, ни соединения.
    explicit module(TPorts io) : io_(std::move(io)) {}

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return module_name;
    }
    [[nodiscard]] version get_version() const noexcept override {
        return Version;
    }

    void initialize(module_context&) override {}
    void start() override {}
    work_status iterate(std::stop_token) override {
        return work_status::idle;  // no-op-итерация и есть простой
    }
    void stop() override {}

    // Узел целиком — для передачи заранее скоммутированных портов.
    [[nodiscard]] TPorts& io() {
        return io_;
    }
    [[nodiscard]] const TPorts& io() const {
        return io_;
    }

    // Ковариантные overrides — авторская точка доступа и контракт базы
    // одновременно: конкретный тип модуля видит свои секции с портами
    // (inputs().step, outputs().count), машинерия через module_base —
    // те же реестры type-erased.
    [[nodiscard]] TPorts::in_type& inputs() override {
        return io_.in;
    }
    [[nodiscard]] const TPorts::in_type& inputs() const override {
        return io_.in;
    }
    [[nodiscard]] TPorts::out_type& outputs() override {
        return io_.out;
    }
    [[nodiscard]] const TPorts::out_type& outputs() const override {
        return io_.out;
    }

   private:
    TPorts io_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_HPP
