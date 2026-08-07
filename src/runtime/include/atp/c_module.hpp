// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_C_MODULE_HPP
#define ATP_RUNTIME_C_MODULE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <atp/plugin_c.h>
#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/module_factory_base.hpp>
#include <atp/module_host.hpp>
#include <atp/module_registry.hpp>

namespace atp {
class c_module;
}

/// The opaque per-instance context of plugin_c.h, defined here because this is the side that owns it:
/// a plugin holds the pointer and hands it back, and only the host ever looks inside.
struct atp_ctx {
    atp::c_module* owner;
};

namespace atp {

/// Symbols of the C registration path, as constants so that the loader and the tests do not
/// duplicate the strings.
inline constexpr const char* c_abi_version_symbol = "atp_c_abi_version";
inline constexpr const char* c_module_count_symbol = "atp_module_count";
inline constexpr const char* c_module_desc_at_symbol = "atp_module_desc_at";

using c_abi_version_fn = unsigned();
using c_module_count_fn = unsigned();
using c_module_desc_at_fn = const atp_module_desc*(unsigned);

namespace detail {

/// Storage backing the byte payloads handed out to a plugin, one per module instance.
///
/// The contract of plugin_c.h is that such a payload stays valid only until the next read on the same
/// context, which is precisely what lets a single reused buffer serve every one of them: no allocation
/// on the hot path once it has grown to the size the ports actually carry. Only the reading callbacks
/// touch it, which is what makes passing a payload straight into write_output legal.
using c_scratch = std::vector<std::byte>;

/// The C++ type each atp_kind names, which is the whole of the mapping between the two type systems.
template <atp_kind Kind>
struct c_type;

template <>
struct c_type<ATP_KIND_I32> {
    using type = std::int32_t;
};
template <>
struct c_type<ATP_KIND_I64> {
    using type = std::int64_t;
};
template <>
struct c_type<ATP_KIND_F64> {
    using type = double;
};
template <>
struct c_type<ATP_KIND_BOOL> {
    using type = bool;
};
template <>
struct c_type<ATP_KIND_TEXT> {
    using type = std::string;
};
template <>
struct c_type<ATP_KIND_BLOB> {
    using type = io::blob;
};

/// Calls @p fn with a std::type_identity of the C++ type behind @p kind — the single switch over the
/// kinds in this file. Everything else (building a port, reading one, writing one) is written once
/// against the type and reached through here, so a new kind is one line in c_type and one here.
/// @throws std::runtime_error on a value that is not a kind
template <typename TFn>
decltype(auto) with_c_kind(atp_kind kind, TFn&& fn) {
    switch (kind) {
        case ATP_KIND_I32:
            return fn(std::type_identity<c_type<ATP_KIND_I32>::type>{});
        case ATP_KIND_I64:
            return fn(std::type_identity<c_type<ATP_KIND_I64>::type>{});
        case ATP_KIND_F64:
            return fn(std::type_identity<c_type<ATP_KIND_F64>::type>{});
        case ATP_KIND_BOOL:
            return fn(std::type_identity<c_type<ATP_KIND_BOOL>::type>{});
        case ATP_KIND_TEXT:
            return fn(std::type_identity<c_type<ATP_KIND_TEXT>::type>{});
        case ATP_KIND_BLOB:
            return fn(std::type_identity<c_type<ATP_KIND_BLOB>::type>{});
    }
    throw std::runtime_error("unknown atp_kind " + std::to_string(static_cast<int>(kind)));
}

/// Conversion of one value between an atp_value and the C++ type of its port. There is no primary
/// definition, so a type reachable through with_c_kind but not convertible is a compile error rather
/// than a silent degradation — the same discipline as io::property_codec.
template <typename T>
struct c_value;

template <>
struct c_value<std::int32_t> {
    static constexpr atp_kind kind = ATP_KIND_I32;
    static std::optional<std::int32_t> load(const atp_value& value) {
        return value.as.i32;
    }
    static void store(const std::int32_t& value, atp_value& out, c_scratch&) {
        out.kind = kind;
        out.as.i32 = value;
    }
};

template <>
struct c_value<std::int64_t> {
    static constexpr atp_kind kind = ATP_KIND_I64;
    static std::optional<std::int64_t> load(const atp_value& value) {
        return value.as.i64;
    }
    static void store(const std::int64_t& value, atp_value& out, c_scratch&) {
        out.kind = kind;
        out.as.i64 = value;
    }
};

template <>
struct c_value<double> {
    static constexpr atp_kind kind = ATP_KIND_F64;
    static std::optional<double> load(const atp_value& value) {
        return value.as.f64;
    }
    static void store(const double& value, atp_value& out, c_scratch&) {
        out.kind = kind;
        out.as.f64 = value;
    }
};

template <>
struct c_value<bool> {
    static constexpr atp_kind kind = ATP_KIND_BOOL;
    static std::optional<bool> load(const atp_value& value) {
        return value.as.boolean != 0;
    }
    static void store(const bool& value, atp_value& out, c_scratch&) {
        out.kind = kind;
        out.as.boolean = value ? 1 : 0;
    }
};

/// Fills the byte view of an atp_value out of the scratch buffer, which is what makes the pointer
/// outlive the call that produced it without anything being allocated for the plugin to free.
inline void store_bytes(const void* data, std::size_t size, atp_kind kind, atp_value& out, c_scratch& scratch) {
    scratch.resize(size);
    if (size > 0) {
        std::memcpy(scratch.data(), data, size);
    }
    out.kind = kind;
    out.as.bytes.data = reinterpret_cast<const char*>(scratch.data());
    out.as.bytes.size = size;
}

template <>
struct c_value<std::string> {
    static constexpr atp_kind kind = ATP_KIND_TEXT;
    static std::optional<std::string> load(const atp_value& value) {
        if (value.as.bytes.data == nullptr) {
            return value.as.bytes.size == 0 ? std::optional<std::string>{std::string{}} : std::nullopt;
        }
        return std::string(value.as.bytes.data, value.as.bytes.size);
    }
    static void store(const std::string& value, atp_value& out, c_scratch& scratch) {
        store_bytes(value.data(), value.size(), kind, out, scratch);
    }
};

template <>
struct c_value<io::blob> {
    static constexpr atp_kind kind = ATP_KIND_BLOB;
    static std::optional<io::blob> load(const atp_value& value) {
        if (value.as.bytes.data == nullptr) {
            return value.as.bytes.size == 0 ? std::optional<io::blob>{io::blob{}} : std::nullopt;
        }
        const auto* first = reinterpret_cast<const std::byte*>(value.as.bytes.data);
        return io::blob(first, first + value.as.bytes.size);
    }
    static void store(const io::blob& value, atp_value& out, c_scratch& scratch) {
        store_bytes(value.data(), value.size(), kind, out, scratch);
    }
};

/// Per-kind operations on one input, selected once at construction and reached by pointer
/// afterwards. The same shape as io::input_base::erased_of<T>(): a table of functions per type
/// instead of a cast down a hierarchy, so nothing on the reading path is a dynamic_cast.
struct c_input_vtable {
    atp_kind kind;
    bool (*get)(io::input_base& port, atp_value& out, c_scratch& scratch);
    bool (*take)(io::input_base& port, atp_value& out, c_scratch& scratch);
};

template <typename T>
[[nodiscard]] const c_input_vtable& c_input_vtable_of() {
    static const c_input_vtable table{
        c_value<T>::kind,
        [](io::input_base& port, atp_value& out, c_scratch& scratch) {
            auto& typed = static_cast<io::input<T>&>(port);
            if (typed.empty()) {
                return false;
            }
            c_value<T>::store(typed.get(), out, scratch);
            return true;
        },
        [](io::input_base& port, atp_value& out, c_scratch& scratch) {
            std::optional<T> taken = static_cast<io::input<T>&>(port).take();
            if (!taken) {
                return false;
            }
            c_value<T>::store(*taken, out, scratch);
            return true;
        },
    };
    return table;
}

/// Per-kind write of one output.
struct c_output_vtable {
    atp_kind kind;
    void (*write)(io::output_base& port, const atp_value& value);
};

template <typename T>
[[nodiscard]] const c_output_vtable& c_output_vtable_of() {
    static const c_output_vtable table{
        c_value<T>::kind,
        [](io::output_base& port, const atp_value& value) {
            std::optional<T> loaded = c_value<T>::load(value);
            if (!loaded) {
                throw std::runtime_error("output '" + port.name() + "': malformed value");
            }
            static_cast<io::output<T>&>(port)(std::move(*loaded));
        },
    };
    return table;
}

/// Per-kind operations on one property.
struct c_property_vtable {
    atp_kind kind;
    void (*get)(io::property_base& port, atp_value& out, c_scratch& scratch);
    bool (*take)(io::property_base& port, atp_value& out, c_scratch& scratch);
    void (*set)(io::property_base& port, const atp_value& value);
};

template <typename T>
[[nodiscard]] const c_property_vtable& c_property_vtable_of() {
    static const c_property_vtable table{
        c_value<T>::kind,
        [](io::property_base& port, atp_value& out, c_scratch& scratch) {
            c_value<T>::store(static_cast<io::property<T>&>(port).get(), out, scratch);
        },
        [](io::property_base& port, atp_value& out, c_scratch& scratch) {
            std::optional<T> taken = static_cast<io::property<T>&>(port).take();
            if (!taken) {
                return false;
            }
            c_value<T>::store(*taken, out, scratch);
            return true;
        },
        [](io::property_base& port, const atp_value& value) {
            std::optional<T> loaded = c_value<T>::load(value);
            if (!loaded) {
                throw std::invalid_argument("property '" + port.name() + "': malformed value");
            }
            static_cast<io::property<T>&>(port)(std::move(*loaded));
        },
    };
    return table;
}

[[nodiscard]] inline std::string_view c_text(const char* text) {
    return text == nullptr ? std::string_view{} : std::string_view{text};
}

/// Rejects a descriptor that cannot be turned into a module, before anything is built from it.
///
/// The checks are all of the "a C struct cannot express this" kind: a required function pointer left
/// null, a count without an array, a kind outside the enumeration. They run at load time, so a
/// malformed plugin fails while the host is still setting up rather than on the first pass.
/// @throws std::runtime_error naming the field
inline void validate_c_desc(const atp_module_desc& desc) {
    // A frozen size, not sizeof: when this struct grows, the constant stays at the v1 layout so that
    // a plugin built against ATP_C_ABI 1 keeps loading into a host that knows more fields.
    if (desc.struct_size < sizeof(atp_module_desc)) {
        throw std::runtime_error("module descriptor struct_size is " + std::to_string(desc.struct_size) +
                                 ", expected at least " + std::to_string(sizeof(atp_module_desc)));
    }
    if (c_text(desc.name).empty()) {
        throw std::runtime_error("module descriptor has no name");
    }
    const std::string where = "module '" + std::string(c_text(desc.name)) + "': ";
    if (desc.version_count > version::max_parts) {
        throw std::runtime_error(where + "version_count is " + std::to_string(desc.version_count) + ", at most " +
                                 std::to_string(version::max_parts) + " parts are representable");
    }
    if (desc.create == nullptr || desc.destroy == nullptr || desc.iterate == nullptr) {
        throw std::runtime_error(where + "create, destroy and iterate are required");
    }
    if ((desc.input_count > 0 && desc.inputs == nullptr) || (desc.output_count > 0 && desc.outputs == nullptr) ||
        (desc.property_count > 0 && desc.properties == nullptr)) {
        throw std::runtime_error(where + "a non-zero port count with a null array");
    }
    for (std::uint32_t i = 0; i < desc.property_count; ++i) {
        if (desc.properties[i].kind == ATP_KIND_BLOB) {
            throw std::runtime_error(where + "property '" + std::string(c_text(desc.properties[i].name)) +
                                     "' is a blob; a property is a scalar edited as text");
        }
    }
}

}  // namespace detail

/// A module the platform drives through the C ABI of plugin_c.h.
///
/// The second hand-written module_base in the tree after group, and for the same reason: its ports
/// are not known at compile time, so atp::module<TPorts, Name, Version> — whose whole point is the
/// declaration being a type — does not apply. Everything the io layer needs is built here from a
/// descriptor instead, which is why no C++ template of the platform has to exist inside the plugin.
///
/// Ports live in the ordinary registries and are ordinary input<T>/output<T>/property<T>, so a module
/// on the far side of this class connects to a C++ module with no adapter in between; the arrays of
/// pointers below are an index for the plugin's benefit, not a second home for the ports.
class c_module final : public module_base {
   public:
    /// Builds the ports and the module state from a validated descriptor.
    /// @param desc descriptor, which must outlive this object — the loader keeps the plugin's
    ///        library pinned for exactly that reason
    /// @throws std::runtime_error if a port cannot be built, or std::invalid_argument if a property
    ///         default is unparsable or outside its own options
    explicit c_module(const atp_module_desc& desc) : desc_(&desc), name_(detail::c_text(desc.name)) {
        for (std::uint32_t i = 0; i < desc.version_count; ++i) {
            version_.parts[i] = desc.version[i];
        }
        version_.count = desc.version_count;
        build_inputs();
        build_outputs();
        build_properties();
        self_ = desc.create(&api(), &ctx_, desc.user_data);
        if (self_ == nullptr) {
            throw std::runtime_error("module '" + name_ + "': create refused");
        }
    }

    ~c_module() override {
        desc_->destroy(self_);
    }

    c_module(const c_module&) = delete;
    c_module& operator=(const c_module&) = delete;

    /// Keeps the host out of the context rather than the context itself: group hands every child a
    /// module_context built on its own stack for the duration of this call, so the aggregate is gone
    /// by the time start() runs and only the references inside it are worth anything afterwards.
    void initialize(module_context& context) override {
        host_ = &context.host;
        call(desc_->initialize, "initialize");
    }

    void start() override {
        call(desc_->start, "start");
    }

    void stop() override {
        call(desc_->stop, "stop");
    }

    work_status iterate(std::stop_token stop_token) override {
        const token_guard guard{*this, stop_token};
        const atp_work answer = desc_->iterate(self_);
        rethrow_pending();
        switch (answer) {
            case ATP_WORK_BUSY:
                return work_status::busy;
            case ATP_WORK_IDLE:
                return work_status::idle;
            case ATP_WORK_ERROR:
                throw std::runtime_error(failure_text("iterate"));
        }
        throw std::runtime_error("module '" + name_ + "': iterate answered " + std::to_string(answer));
    }

    [[nodiscard]] std::string_view get_name() const noexcept override {
        return name_;
    }
    [[nodiscard]] version get_version() const noexcept override {
        return version_;
    }

    [[nodiscard]] io::inputs& inputs() override {
        return in_;
    }
    [[nodiscard]] const io::inputs& inputs() const override {
        return in_;
    }
    [[nodiscard]] io::outputs& outputs() override {
        return out_;
    }
    [[nodiscard]] const io::outputs& outputs() const override {
        return out_;
    }
    [[nodiscard]] io::properties& properties() override {
        return props_;
    }
    [[nodiscard]] const io::properties& properties() const override {
        return props_;
    }

   private:
    struct input_entry {
        io::input_base* port;
        const detail::c_input_vtable* ops;
    };
    struct output_entry {
        io::output_base* port;
        const detail::c_output_vtable* ops;
    };
    struct property_entry {
        io::property_base* port;
        const detail::c_property_vtable* ops;
    };

    /// Holds the stop token of the pass in progress, so that stop_requested() can answer without the
    /// token appearing in the C ABI. Outside a pass there is nothing to ask, and the answer is no.
    class token_guard {
       public:
        token_guard(c_module& owner, const std::stop_token& token) : owner_(owner) {
            owner_.token_ = &token;
        }
        ~token_guard() {
            owner_.token_ = nullptr;
        }
        token_guard(const token_guard&) = delete;
        token_guard& operator=(const token_guard&) = delete;

       private:
        c_module& owner_;
    };

    void build_inputs() {
        for (std::uint32_t i = 0; i < desc_->input_count; ++i) {
            const atp_input_desc& d = desc_->inputs[i];
            const std::string name{detail::c_text(d.name)};
            input_entry entry = detail::with_c_kind(d.kind, [&](auto tag) {
                using T = typename decltype(tag)::type;
                io::input_base* port = nullptr;
                if (d.flavor != ATP_QUEUE) {
                    port = &in_.make<io::input<T>>(name);
                } else if (d.capacity == 0) {
                    port = &in_.make<io::queued_input<T>>(name);
                } else {
                    const io::queue_limit limit{d.capacity, d.overflow == ATP_DROP_INCOMING
                                                                ? io::overflow_policy::drop_incoming
                                                                : io::overflow_policy::drop_oldest};
                    port = &in_.make<io::queued_input<T>>(name, limit);
                }
                return input_entry{port, &detail::c_input_vtable_of<T>()};
            });
            input_index_.push_back(entry);
        }
    }

    void build_outputs() {
        for (std::uint32_t i = 0; i < desc_->output_count; ++i) {
            const atp_output_desc& d = desc_->outputs[i];
            const std::string name{detail::c_text(d.name)};
            output_entry entry = detail::with_c_kind(d.kind, [&](auto tag) {
                using T = typename decltype(tag)::type;
                return output_entry{&out_.make<io::output<T>>(name), &detail::c_output_vtable_of<T>()};
            });
            output_index_.push_back(entry);
        }
    }

    /// Declares the properties, and is the one place where a kind is refused rather than dispatched.
    ///
    /// A property needs an io::property_codec, which the byte payload of ATP_KIND_BLOB deliberately
    /// has none of — "a setting a human edits as text" and "opaque bytes" are incompatible by
    /// intent. The concept has to be honoured at compile time, since the dispatch below instantiates
    /// every kind whatever a descriptor actually asks for, so validate_c_desc rejecting a blob
    /// property at load time is not enough on its own: the unreachable branch is what makes this
    /// compile, and the guard in front of it is what keeps the branch unreachable.
    void build_properties() {
        for (std::uint32_t i = 0; i < desc_->property_count; ++i) {
            const atp_property_desc& d = desc_->properties[i];
            const std::string name{detail::c_text(d.name)};
            if (d.kind == ATP_KIND_BLOB) {
                throw std::runtime_error("module '" + name_ + "': property '" + name + "' cannot be a blob");
            }
            property_entry entry = detail::with_c_kind(d.kind, [&](auto tag) {
                using T = typename decltype(tag)::type;
                if constexpr (io::property_value<T>) {
                    return property_entry{&make_property<T>(name, d), &detail::c_property_vtable_of<T>()};
                } else {
                    return property_entry{nullptr, nullptr};
                }
            });
            property_index_.push_back(entry);
        }
    }

    /// Declares one property, parsing the default and the options out of their string forms through
    /// the very codec a config value goes through — so that "5" against "\"5\"", an out-of-range
    /// number and a value outside the options are all one error, reported the same way here as they
    /// are for a C++ module.
    template <typename T>
    io::property_base& make_property(const std::string& name, const atp_property_desc& d) {
        const io::persistence keep = d.persistent != 0 ? io::persistent : io::transient;
        std::optional<T> fallback = io::property_codec<T>::from_string(detail::c_text(d.default_value));
        if (!fallback) {
            throw std::invalid_argument("module '" + name_ + "': property '" + name + "' has an unparsable default '" +
                                        std::string(detail::c_text(d.default_value)) + "'");
        }
        if (d.options == nullptr || d.option_count == 0) {
            return props_.make<io::property<T>>(name, std::move(*fallback), keep);
        }
        io::option_set<T> allowed;
        allowed.values.reserve(d.option_count);
        for (std::uint32_t i = 0; i < d.option_count; ++i) {
            std::optional<T> option = io::property_codec<T>::from_string(detail::c_text(d.options[i]));
            if (!option) {
                throw std::invalid_argument("module '" + name_ + "': property '" + name +
                                            "' has an unparsable option '" + std::string(detail::c_text(d.options[i])) +
                                            "'");
            }
            allowed.values.push_back(std::move(*option));
        }
        return props_.make<io::property<T>>(name, std::move(*fallback), allowed, keep);
    }

    void call(atp_lifecycle_fn step, const char* what) {
        if (step == nullptr) {
            return;
        }
        const atp_status status = step(self_);
        rethrow_pending();
        if (status != ATP_OK) {
            throw std::runtime_error(failure_text(what));
        }
    }

    /// Message of the error a failed call is turned into: whatever the module said through
    /// set_error, with the module's own name in front of it, which the module is told not to repeat.
    [[nodiscard]] std::string failure_text(const char* what) {
        std::string text = "module '" + name_ + "': " + what + " failed";
        if (!error_.empty()) {
            text += ": " + error_;
            error_.clear();
        }
        return text;
    }

    /// Raises an error that happened inside a callback rather than inside the plugin.
    ///
    /// A callback cannot let an exception escape into a foreign frame, so it stores it and answers
    /// failure. Rethrowing here rather than trusting the plugin's return code is what keeps a module
    /// that ignores a failed write from swallowing a host-side error: the pass fails either way.
    void rethrow_pending() {
        if (pending_ == nullptr) {
            return;
        }
        const std::exception_ptr raised = std::exchange(pending_, nullptr);
        std::rethrow_exception(raised);
    }

    /// Runs a callback body, converting anything it throws into a stored exception and a refusal.
    template <typename TFn>
    static int guarded(atp_ctx* ctx, TFn&& body) noexcept {
        if (ctx == nullptr || ctx->owner == nullptr) {
            return 0;
        }
        c_module& self = *ctx->owner;
        try {
            return body(self) ? 1 : 0;
        } catch (...) {
            if (self.pending_ == nullptr) {
                self.pending_ = std::current_exception();
            }
            return 0;
        }
    }

    [[nodiscard]] const input_entry* input_at(std::uint32_t i) const {
        return i < input_index_.size() ? &input_index_[i] : nullptr;
    }
    [[nodiscard]] const output_entry* output_at(std::uint32_t i) const {
        return i < output_index_.size() ? &output_index_[i] : nullptr;
    }
    [[nodiscard]] const property_entry* property_at(std::uint32_t i) const {
        return i < property_index_.size() ? &property_index_[i] : nullptr;
    }

    /// The callback table handed to every instance. One object for the whole process: the callbacks
    /// are stateless and reach their instance through the context they are given.
    [[nodiscard]] static const atp_api& api() {
        static const atp_api table = make_api();
        return table;
    }

    static atp_api make_api() {
        atp_api table{};
        table.struct_size = sizeof(atp_api);
        table.get_input = [](atp_ctx* ctx, std::uint32_t i, atp_value* out) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const input_entry* entry = self.input_at(i);
                return entry != nullptr && out != nullptr && entry->ops->get(*entry->port, *out, self.scratch_);
            });
        };
        table.take_input = [](atp_ctx* ctx, std::uint32_t i, atp_value* out) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const input_entry* entry = self.input_at(i);
                return entry != nullptr && out != nullptr && entry->ops->take(*entry->port, *out, self.scratch_);
            });
        };
        table.write_output = [](atp_ctx* ctx, std::uint32_t i, const atp_value* value) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const output_entry* entry = self.output_at(i);
                if (entry == nullptr || value == nullptr || value->kind != entry->ops->kind) {
                    return false;
                }
                entry->ops->write(*entry->port, *value);
                return true;
            });
        };
        table.get_property = [](atp_ctx* ctx, std::uint32_t i, atp_value* out) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const property_entry* entry = self.property_at(i);
                if (entry == nullptr || out == nullptr) {
                    return false;
                }
                entry->ops->get(*entry->port, *out, self.scratch_);
                return true;
            });
        };
        table.take_property = [](atp_ctx* ctx, std::uint32_t i, atp_value* out) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const property_entry* entry = self.property_at(i);
                return entry != nullptr && out != nullptr && entry->ops->take(*entry->port, *out, self.scratch_);
            });
        };
        table.set_property = [](atp_ctx* ctx, std::uint32_t i, const atp_value* value) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const property_entry* entry = self.property_at(i);
                if (entry == nullptr || value == nullptr || value->kind != entry->ops->kind) {
                    return false;
                }
                entry->ops->set(*entry->port, *value);
                return true;
            });
        };
        table.stop_requested = [](atp_ctx* ctx) noexcept {
            return guarded(ctx, [](c_module& self) { return self.token_ != nullptr && self.token_->stop_requested(); });
        };
        table.log = [](atp_ctx* ctx, atp_log_level level, const char* text, std::size_t len) noexcept {
            guarded(ctx, [&](c_module& self) {
                if (self.host_ == nullptr || text == nullptr) {
                    return false;
                }
                if (level < ATP_LOG_ERROR || level > ATP_LOG_DEBUG) {
                    return false;
                }
                self.host_->log(static_cast<log_level>(level), std::string_view{text, len});
                return true;
            });
        };
        table.wake = [](atp_ctx* ctx) noexcept {
            guarded(ctx, [](c_module& self) {
                if (self.host_ == nullptr) {
                    return false;
                }
                self.host_->wake();
                return true;
            });
        };
        table.set_error = [](atp_ctx* ctx, const char* text, std::size_t len) noexcept {
            guarded(ctx, [&](c_module& self) {
                self.error_.assign(text == nullptr ? "" : text, text == nullptr ? 0 : len);
                return true;
            });
        };
        return table;
    }

    const atp_module_desc* desc_;
    std::string name_;
    version version_{};

    io::inputs in_;
    io::outputs out_;
    io::properties props_;
    std::vector<input_entry> input_index_;
    std::vector<output_entry> output_index_;
    std::vector<property_entry> property_index_;

    atp_ctx ctx_{this};
    void* self_ = nullptr;
    module_host* host_ = nullptr;
    detail::c_scratch scratch_;
    std::string error_;
    std::exception_ptr pending_;
    mutable const std::stop_token* token_ = nullptr;
};

/// Factory of one C-ABI module, registered per descriptor.
///
/// Holds the descriptor by pointer and nothing else: the plugin's library is kept loaded by the pin
/// module_registrar puts around this factory, so the descriptor and every string it points at stay
/// valid for as long as any module created here is alive.
class c_module_factory final : public module_factory_base {
   public:
    /// @param desc descriptor to build modules from
    /// @throws std::runtime_error if the descriptor is malformed
    explicit c_module_factory(const atp_module_desc& desc) : desc_(&desc), name_(detail::c_text(desc.name)) {
        detail::validate_c_desc(desc);
        for (std::uint32_t i = 0; i < desc.version_count; ++i) {
            version_.parts[i] = desc.version[i];
        }
        version_.count = desc.version_count;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] version get_version() const noexcept override {
        return version_;
    }

    [[nodiscard]] module_ptr create() const override {
        return module_ptr(new c_module(*desc_), module_deleter{});
    }

   private:
    const atp_module_desc* desc_;
    std::string name_;
    version version_{};
};

/// Registers every module a C-ABI plugin offers.
///
/// The descriptors are pulled rather than pushed — the plugin exposes a table and this walks it — so
/// nothing of the host travels into the plugin at load time and a partial registration is impossible
/// to reach by reentering.
/// @param registrar registrar of the loading plugin, which is what puts the library pin around each
///        factory and lets the loader withdraw them again on unload
/// @param abi the plugin's atp_c_abi_version
/// @param count the plugin's atp_module_count
/// @param at the plugin's atp_module_desc_at
/// @throws std::runtime_error on an ABI mismatch, a null descriptor or a malformed one
inline void register_c_modules(module_registrar& registrar,
                               c_abi_version_fn* abi,
                               c_module_count_fn* count,
                               c_module_desc_at_fn* at) {
    if (const unsigned plugin_version = abi(); plugin_version != ATP_C_ABI) {
        throw std::runtime_error("plugin has C ABI " + std::to_string(plugin_version) + ", host expects " +
                                 std::to_string(ATP_C_ABI));
    }
    const unsigned total = count();
    for (unsigned i = 0; i < total; ++i) {
        const atp_module_desc* desc = at(i);
        if (desc == nullptr) {
            throw std::runtime_error("module descriptor " + std::to_string(i) + " is null");
        }
        registrar.add(std::make_unique<c_module_factory>(*desc));
    }
}

}  // namespace atp

#endif
