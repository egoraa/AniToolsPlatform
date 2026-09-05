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
#include <atp/hosting/module_factory_base.hpp>
#include <atp/hosting/module_registrar.hpp>
#include <atp/io.hpp>
#include <atp/module/module_base.hpp>
#include <atp/module/module_host.hpp>
#include <atp/runtime/c_config.hpp>
#include <atp/runtime/config_path.hpp>
#include <atp/runtime/config_tree_source.hpp>
#include <atp/runtime/raw_config.hpp>

namespace atp::runtime {
class c_module;
}

/// The opaque per-instance context of plugin_c.h, defined here because this is the side that owns it:
/// a plugin holds the pointer and hands it back, and only the host ever looks inside.
struct atp_ctx {
    atp::runtime::c_module* owner;
};

namespace atp::runtime {

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
/// A payload stays valid only until the next read on the same context, which is what lets a single
/// reused buffer serve every one of them: no allocation on the hot path once it has grown to the size
/// the ports carry. Only the reading callbacks touch it, which makes passing a payload straight into
/// write_output legal.
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
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_I32>::type>{});
        case ATP_KIND_I64:
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_I64>::type>{});
        case ATP_KIND_F64:
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_F64>::type>{});
        case ATP_KIND_BOOL:
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_BOOL>::type>{});
        case ATP_KIND_TEXT:
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_TEXT>::type>{});
        case ATP_KIND_BLOB:
            return std::forward<TFn>(fn)(std::type_identity<c_type<ATP_KIND_BLOB>::type>{});
    }
    throw std::runtime_error("unknown atp_kind " + std::to_string(static_cast<int>(kind)));
}

/// Conversion of one value between an atp_value and the C++ type of its port. There is no primary
/// definition, so a type reachable through with_c_kind but not convertible is a compile error rather
/// than a silent degradation.
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
/// afterwards: a table of functions per type instead of a cast down a hierarchy, so nothing on the
/// reading path is a dynamic_cast.
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

/// Reads a field the v1 layout did not have, or nothing when the plugin is older than the field.
///
/// The size check is the whole point of struct_size and belongs here rather than at each use: a
/// plugin built against ATP_C_ABI 1 hands over a shorter object, and reading past what it declared
/// would be reading its neighbours.
/// @param desc descriptor as the plugin declared it
/// @return the source path, empty when absent or not declared
[[nodiscard]] inline std::string_view c_desc_source(const atp_module_desc& desc) {
    if (desc.struct_size < offsetof(atp_module_desc, source) + sizeof(desc.source)) {
        return {};
    }
    return c_text(desc.source);
}

/// The config declaration of a descriptor, empty for a plugin built before the field existed.
///
/// Read behind the size check for the same reason c_desc_source is: struct_size says how much of the
/// struct the plugin actually wrote, and reading past it would read whatever happened to follow.
[[nodiscard]] inline std::span<const atp_config_field_desc> c_desc_config_fields(const atp_module_desc& desc) {
    if (desc.struct_size < offsetof(atp_module_desc, config_field_count) + sizeof(desc.config_field_count)) {
        return {};
    }
    if (desc.config_fields == nullptr) {
        return {};
    }
    return {desc.config_fields, desc.config_field_count};
}

/// The kind field of a descriptor as the integer it may not be a value of.
///
/// Copied out rather than read: atp_kind names 1 to 6, so its value range is 0 to 7, and a plugin
/// that wrote anything else into the field makes an ordinary load of it undefined — which is the
/// very case this validation exists to catch, and one -fsanitize=undefined reports. The enumeration
/// cannot be given a fixed underlying type instead, because plugin_c.h has to keep compiling as C99.
[[nodiscard]] inline int c_kind_value(const atp_kind& kind) noexcept {
    static_assert(sizeof(atp_kind) == sizeof(int));
    int value = 0;
    std::memcpy(&value, &kind, sizeof(value));
    return value;
}

/// Whether @p kind is one of the six values atp_kind names.
///
/// Takes the integer rather than the enumeration, and that is the whole point: the value being
/// checked is one atp_kind does not name, so a switch over the enumeration is exhaustive as far as
/// the compiler is concerned and the "none of them" branch is dead code it may fold away. The six
/// values are still spelled out one by one, because they are the contract of a separate C header.
[[nodiscard]] inline bool is_c_kind(int kind) noexcept {
    switch (kind) {
        case ATP_KIND_I32:
        case ATP_KIND_I64:
        case ATP_KIND_F64:
        case ATP_KIND_BOOL:
        case ATP_KIND_TEXT:
        case ATP_KIND_BLOB:
            return true;
        default:
            return false;
    }
}

/// Rejects a descriptor that cannot be turned into a module, before anything is built from it.
///
/// The checks are all of the "a C struct cannot express this" kind: a required function pointer left
/// null, a count without an array, a kind outside the enumeration. They run at load time, so a
/// malformed plugin fails while the host is setting up rather than on the first pass. The kinds
/// checked here are the ports' own; the kind of a config field is checked where the config is built
/// (c_config::to_field_kind), because reaching it from here would mean walking the whole field tree
/// twice.
/// @throws std::runtime_error naming the field
inline void validate_c_desc(const atp_module_desc& desc) {
    if (desc.struct_size < ATP_MODULE_DESC_SIZE_V1) {
        throw std::runtime_error("module descriptor struct_size is " + std::to_string(desc.struct_size) +
                                 ", expected at least " + std::to_string(ATP_MODULE_DESC_SIZE_V1));
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
    for (std::uint32_t i = 0; i < desc.input_count; ++i) {
        if (!is_c_kind(c_kind_value(desc.inputs[i].kind))) {
            throw std::runtime_error(where + "input '" + std::string(c_text(desc.inputs[i].name)) + "' has kind " +
                                     std::to_string(c_kind_value(desc.inputs[i].kind)) + ", which is outside atp_kind");
        }
    }
    for (std::uint32_t i = 0; i < desc.output_count; ++i) {
        if (!is_c_kind(c_kind_value(desc.outputs[i].kind))) {
            throw std::runtime_error(where + "output '" + std::string(c_text(desc.outputs[i].name)) + "' has kind " +
                                     std::to_string(c_kind_value(desc.outputs[i].kind)) +
                                     ", which is outside atp_kind");
        }
    }
    for (std::uint32_t i = 0; i < desc.property_count; ++i) {
        if (!is_c_kind(c_kind_value(desc.properties[i].kind))) {
            throw std::runtime_error(where + "property '" + std::string(c_text(desc.properties[i].name)) +
                                     "' has kind " + std::to_string(c_kind_value(desc.properties[i].kind)) +
                                     ", which is outside atp_kind");
        }
        if (desc.properties[i].kind == ATP_KIND_BLOB) {
            throw std::runtime_error(where + "property '" + std::string(c_text(desc.properties[i].name)) +
                                     "' is a blob; a property is a scalar edited as text");
        }
    }
}

/// config::kind -> atp_config_kind. The two enumerations list the same seven forms in the same order,
/// and this spells the mapping out anyway rather than casting: the C header is a separate contract,
/// so a reordering there has to fail here instead of silently renaming every form.
[[nodiscard]] inline int c_config_kind(atp::config::kind kind) noexcept {
    switch (kind) {
        case atp::config::kind::null:
            return ATP_CONFIG_NULL;
        case atp::config::kind::boolean:
            return ATP_CONFIG_BOOL;
        case atp::config::kind::integer:
            return ATP_CONFIG_INT;
        case atp::config::kind::real:
            return ATP_CONFIG_REAL;
        case atp::config::kind::string:
            return ATP_CONFIG_TEXT;
        case atp::config::kind::array:
            return ATP_CONFIG_ARRAY;
        case atp::config::kind::object:
            return ATP_CONFIG_OBJECT;
    }
    return ATP_CONFIG_NULL;
}

/// Reads a scalar config node into an atp_value, refusing the three forms atp_kind cannot name.
///
/// Text points straight into the node's own std::string, which lives as long as the module does — no
/// copy into the scratch buffer, so a config string cannot be invalidated by an unrelated port read.
/// @return false for null, an array or an object, leaving @p out untouched
[[nodiscard]] inline bool c_config_scalar(const atp::config::node& value, atp_value& out) {
    switch (value.kind()) {
        case atp::config::kind::boolean:
            out.kind = ATP_KIND_BOOL;
            out.as.boolean = value.as_bool() ? 1 : 0;
            return true;
        case atp::config::kind::integer:
            out.kind = ATP_KIND_I64;
            out.as.i64 = value.as_int();
            return true;
        case atp::config::kind::real:
            out.kind = ATP_KIND_F64;
            out.as.f64 = value.as_double();
            return true;
        case atp::config::kind::string: {
            const std::string* text = value.string_ptr();
            out.kind = ATP_KIND_TEXT;
            out.as.bytes.data = text->data();
            out.as.bytes.size = text->size();
            return true;
        }
        case atp::config::kind::null:
        case atp::config::kind::array:
        case atp::config::kind::object:
            return false;
    }
    return false;
}

}  // namespace detail

/// A module the platform drives through the C ABI of plugin_c.h.
///
/// Hand-written rather than an atp::module<TPorts, Name, Version>, whose whole point is the
/// declaration being a type: here the ports are known only at run time, from a descriptor. Building
/// them on this side is what keeps every C++ template of the platform out of the plugin.
///
/// The ports are ordinary input<T>/output<T>/property<T> in the ordinary registries, so a module on
/// the far side of this class connects to a C++ one with no adapter in between; the arrays of
/// pointers below are an index for the plugin's benefit, not a second home for the ports.
class c_module final : public module_base {
   public:
    /// Builds the ports and the module state from a validated descriptor.
    ///
    /// The config is stored and indexed before desc.create runs, because that call is this path's
    /// analogue of a constructor and the module is entitled to read its config from inside it.
    ///
    /// A host-side failure inside a callback the plugin made from create is raised here, and the
    /// instance the plugin managed to build is destroyed first: a throw from a constructor runs no
    /// destructor, so nothing else would ever call desc.destroy on it. Raising it here rather than
    /// leaving it pending keeps the blame where it belongs — the next lifecycle call would otherwise
    /// present a config error as a failure of initialize.
    /// @param desc descriptor, which must outlive this object — the loader keeps the plugin's library
    ///        pinned for exactly that reason
    /// @param config config for this instance, readable through the api's config_* callbacks; owned
    ///        from here on, and never null — the factory refuses a config it did not make
    /// @throws std::runtime_error if a port cannot be built, std::invalid_argument if a property
    ///         default is unparsable or outside its own options, or whatever a callback raised
    c_module(const atp_module_desc& desc, std::unique_ptr<atp::module_config> config)
        : desc_(&desc), name_(detail::c_text(desc.name)), config_(std::move(config)) {
        tree_ = dynamic_cast<const config_tree_source*>(config_.get());
        if (tree_ == nullptr) {
            throw atp::config::access_error("a C module was handed a config that carries no tree");
        }
        for (std::uint32_t i = 0; i < desc.version_count; ++i) {
            version_.parts[i] = desc.version[i];
        }
        version_.count = desc.version_count;
        index_config(tree_->tree());
        build_inputs();
        build_outputs();
        build_properties();
        self_ = desc.create(&api(), &ctx_, desc.user_data);
        if (pending_ != nullptr) {
            if (self_ != nullptr) {
                desc_->destroy(self_);
                self_ = nullptr;
            }
            rethrow_pending();
        }
        if (self_ == nullptr) {
            throw std::runtime_error(failure_text("create refused", ""));
        }
    }

    ~c_module() override {
        desc_->destroy(self_);
    }

    c_module(const c_module&) = delete;
    c_module& operator=(const c_module&) = delete;

    /// Keeps the host out of the context rather than the context itself: a module_context may be built
    /// on the caller's stack for the duration of this call, so it is gone by the time start() runs and
    /// only the references inside it are worth anything afterwards.
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
                using payload = decltype(tag)::type;
                io::input_base* port = nullptr;
                if (d.flavor != ATP_QUEUE) {
                    port = &in_.make<io::input<payload>>(name);
                } else if (d.capacity == 0) {
                    port = &in_.make<io::queued_input<payload>>(name);
                } else {
                    const io::queue_limit limit{d.capacity, d.overflow == ATP_DROP_INCOMING
                                                                ? io::overflow_policy::drop_incoming
                                                                : io::overflow_policy::drop_oldest};
                    port = &in_.make<io::queued_input<payload>>(name, limit);
                }
                return input_entry{port, &detail::c_input_vtable_of<payload>()};
            });
            input_index_.push_back(entry);
        }
    }

    void build_outputs() {
        for (std::uint32_t i = 0; i < desc_->output_count; ++i) {
            const atp_output_desc& d = desc_->outputs[i];
            const std::string name{detail::c_text(d.name)};
            output_entry entry = detail::with_c_kind(d.kind, [&](auto tag) {
                using payload = decltype(tag)::type;
                return output_entry{&out_.make<io::output<payload>>(name), &detail::c_output_vtable_of<payload>()};
            });
            output_index_.push_back(entry);
        }
    }

    /// Declares the properties, and is the one place where a kind is refused rather than dispatched.
    ///
    /// A property needs an io::property_codec, which the byte payload of ATP_KIND_BLOB deliberately
    /// has none of. The dispatch below instantiates every kind whatever a descriptor asks for, so
    /// rejecting a blob property at load time is not enough on its own: the unreachable branch is what
    /// makes this compile, and the guard in front of it is what keeps the branch unreachable.
    void build_properties() {
        for (std::uint32_t i = 0; i < desc_->property_count; ++i) {
            const atp_property_desc& d = desc_->properties[i];
            const std::string name{detail::c_text(d.name)};
            if (d.kind == ATP_KIND_BLOB) {
                throw std::runtime_error("module '" + name_ + "': property '" + name + "' cannot be a blob");
            }
            property_entry entry = detail::with_c_kind(d.kind, [&](auto tag) {
                using payload = decltype(tag)::type;
                if constexpr (io::property_value<payload>) {
                    return property_entry{&make_property<payload>(name, d), &detail::c_property_vtable_of<payload>()};
                } else {
                    return property_entry{nullptr, nullptr};
                }
            });
            property_index_.push_back(entry);
        }
    }

    /// Declares one property, parsing the default and the options out of their string forms through
    /// the very codec a C++ module's values go through, so that an unparsable text, an out-of-range
    /// number and a value outside the options are one error reported the same way on both paths.
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

    /// Message of the error a failed call is turned into: whatever the module said through set_error,
    /// with the module's own name in front of it, which the module is told not to repeat.
    ///
    /// @param what the step that failed
    /// @param suffix what to put after it, "failed" for a lifecycle call and empty where @p what is a
    ///        whole phrase already — a refused create says "create refused", and the set_error text
    ///        must survive there too, being the only explanation such a failure ever carries
    [[nodiscard]] std::string failure_text(const char* what, const char* suffix = " failed") {
        std::string text = "module '" + name_ + "': " + what + suffix;
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

    /// Runs a callback body that answers with a value rather than success or failure, converting
    /// anything it throws into a stored exception and @p on_failure.
    ///
    /// Separate from guarded() below because that one is boolean by construction — it reduces the
    /// body's answer to 1 or 0, which is right for a callback reporting success and wrong for one
    /// returning a handle or a kind: ATP_CONFIG_OBJECT would come back as 1, that is ATP_CONFIG_BOOL,
    /// and every handle would come back as the root's. The failure value is passed in rather than
    /// defaulted, since it is ATP_CONFIG_NONE for a handle and ATP_CONFIG_NULL for a kind.
    template <typename TRet, typename TFn>
    static TRet guarded_value(atp_ctx* ctx, TRet on_failure, TFn&& body) noexcept {
        if (ctx == nullptr || ctx->owner == nullptr) {
            return on_failure;
        }
        c_module& self = *ctx->owner;
        try {
            return std::forward<TFn>(body)(self);
        } catch (...) {
            if (self.pending_ == nullptr) {
                self.pending_ = std::current_exception();
            }
            return on_failure;
        }
    }

    /// Runs a callback body, converting anything it throws into a stored exception and a refusal.
    template <typename TFn>
    static int guarded(atp_ctx* ctx, TFn&& body) noexcept {
        if (ctx == nullptr || ctx->owner == nullptr) {
            return 0;
        }
        c_module& self = *ctx->owner;
        try {
            return std::forward<TFn>(body)(self) ? 1 : 0;
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

    /// Flattens the config into a preorder list of pointers, so a foreign module can address a node by
    /// an opaque number instead of a path. Built once, before desc.create runs, and never again — the
    /// entries point inside config_, which is why config_ must be in place first.
    void index_config(const atp::config::node& node) {
        nodes_.push_back(&node);
        for (std::size_t i = 0; i < node.size(); ++i) {
            index_config(node[i]);
        }
    }

    /// Handle -> node. A handle is the preorder index plus one, which leaves zero free for
    /// ATP_CONFIG_NONE and gives the root 1.
    [[nodiscard]] const atp::config::node* config_at(std::uint32_t node) const {
        return node != ATP_CONFIG_NONE && node <= nodes_.size() ? nodes_[node - 1] : nullptr;
    }

    /// Node -> handle, by scanning the flat list. Linear on purpose: a config is tens of nodes read
    /// once in the constructor, and the bookkeeping that would make this direct costs more than the
    /// scan saves.
    [[nodiscard]] std::uint32_t handle_of(const atp::config::node* node) const {
        const auto at = std::ranges::find(nodes_, node);
        return at == nodes_.end() ? ATP_CONFIG_NONE : static_cast<std::uint32_t>(std::distance(nodes_.begin(), at) + 1);
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
        table.config_root = [](atp_ctx* ctx) noexcept {
            return guarded_value<std::uint32_t>(ctx, ATP_CONFIG_NONE, [](c_module& self) -> std::uint32_t {
                return self.nodes_.empty() ? ATP_CONFIG_NONE : 1u;
            });
        };
        table.config_kind = [](atp_ctx* ctx, std::uint32_t node) noexcept {
            return guarded_value<int>(ctx, ATP_CONFIG_NULL, [&](c_module& self) -> int {
                const atp::config::node* value = self.config_at(node);
                return value == nullptr ? ATP_CONFIG_NULL : detail::c_config_kind(value->kind());
            });
        };
        table.config_size = [](atp_ctx* ctx, std::uint32_t node) noexcept {
            return guarded_value<std::uint32_t>(ctx, 0u, [&](c_module& self) -> std::uint32_t {
                const atp::config::node* value = self.config_at(node);
                return value == nullptr ? 0u : static_cast<std::uint32_t>(value->size());
            });
        };
        table.config_key_at = [](atp_ctx* ctx, std::uint32_t node, std::uint32_t i, const char** out,
                                 std::size_t* len) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const atp::config::node* value = self.config_at(node);
                if (value == nullptr || out == nullptr || len == nullptr || !value->is_object() || i >= value->size()) {
                    return false;
                }
                const std::string_view key = value->key_at(i);
                *out = key.data();
                *len = key.size();
                return true;
            });
        };
        table.config_child_at = [](atp_ctx* ctx, std::uint32_t node, std::uint32_t i) noexcept {
            return guarded_value<std::uint32_t>(ctx, ATP_CONFIG_NONE, [&](c_module& self) -> std::uint32_t {
                const atp::config::node* value = self.config_at(node);
                if (value == nullptr || i >= value->size()) {
                    return ATP_CONFIG_NONE;
                }
                return self.handle_of(&(*value)[i]);
            });
        };
        table.config_find = [](atp_ctx* ctx, std::uint32_t node, const char* key, std::size_t len) noexcept {
            return guarded_value<std::uint32_t>(ctx, ATP_CONFIG_NONE, [&](c_module& self) -> std::uint32_t {
                const atp::config::node* parent = self.config_at(node);
                if (parent == nullptr || key == nullptr || !parent->is_object()) {
                    return ATP_CONFIG_NONE;
                }
                const atp::config::node* found = parent->find(std::string_view(key, len));
                return found == nullptr ? ATP_CONFIG_NONE : self.handle_of(found);
            });
        };
        table.config_value_of = [](atp_ctx* ctx, std::uint32_t node, atp_value* out) noexcept {
            return guarded(ctx, [&](c_module& self) {
                const atp::config::node* value = self.config_at(node);
                if (value == nullptr || out == nullptr) {
                    return false;
                }
                return detail::c_config_scalar(*value, *out);
            });
        };
        table.config_find_path = [](atp_ctx* ctx, std::uint32_t node, const char* path, std::size_t len) noexcept {
            return guarded_value<std::uint32_t>(ctx, ATP_CONFIG_NONE, [&](c_module& self) -> std::uint32_t {
                if (path == nullptr || self.config_at(node) != &self.tree_->tree()) {
                    return ATP_CONFIG_NONE;
                }
                const atp::config::node* found = find_path(self.tree_->tree(), std::string_view(path, len));
                return found == nullptr ? ATP_CONFIG_NONE : self.handle_of(found);
            });
        };
        table.config_text = [](atp_ctx* ctx, const char** out, std::size_t* len) noexcept {
            return guarded(ctx, [&](c_module& self) {
                if (self.config_->origin().empty() || out == nullptr || len == nullptr) {
                    return false;
                }
                *out = self.config_->text().c_str();
                *len = self.config_->text().size();
                return true;
            });
        };
        table.config_origin = [](atp_ctx* ctx, const char** out, std::size_t* len) noexcept {
            return guarded(ctx, [&](c_module& self) {
                if (self.config_->origin().empty() || out == nullptr || len == nullptr) {
                    return false;
                }
                *out = self.config_->origin().c_str();
                *len = self.config_->origin().size();
                return true;
            });
        };
        table.config_is_opaque = [](atp_ctx* ctx) noexcept {
            return guarded_value<int>(ctx, 0, [](c_module& self) -> int { return self.config_->is_opaque() ? 1 : 0; });
        };
        return table;
    }

    const atp_module_desc* desc_;
    std::string name_;
    version version_;

    io::inputs in_;
    io::outputs out_;
    io::properties props_;
    std::vector<input_entry> input_index_;
    std::vector<output_entry> output_index_;
    std::vector<property_entry> property_index_;

    std::unique_ptr<atp::module_config> config_;

    /// The same object under the name that answers the tree; owned through config_ above.
    const config_tree_source* tree_ = nullptr;
    std::vector<const atp::config::node*> nodes_;

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
    explicit c_module_factory(const atp_module_desc& desc) : desc_(&desc) {
        detail::validate_c_desc(desc);
        name_ = detail::c_text(desc.name);
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

    /// A module that declares fields gets a config that carries them; one that declares none gets the
    /// document whole, exactly as the whole C path did before declarations existed.
    [[nodiscard]] config_ptr make_config() const override {
        const std::span<const atp_config_field_desc> fields = detail::c_desc_config_fields(*desc_);
        if (fields.empty()) {
            return config_ptr(new raw_config, config_deleter{});
        }
        return config_ptr(new c_config(fields), config_deleter{});
    }

    /// The config must be one make_config() handed out; another module's is refused rather than
    /// reinterpreted, and carrying a tree is exactly what tells the two apart from everything else.
    ///
    /// A declared config is rendered into its tree here — after load_fields filled it and before
    /// c_module indexes it, which has to happen before desc.create, since a C module reads its config
    /// from inside what is its constructor.
    [[nodiscard]] module_ptr create(config_ptr config) const override {
        if (dynamic_cast<config_tree_source*>(config.get()) == nullptr) {
            throw atp::config::access_error("a C module was handed a config of another module");
        }
        if (auto* declared = dynamic_cast<c_config*>(config.get())) {
            declared->materialize();
        }
        std::unique_ptr<atp::module_config> owned(config.release());
        return module_ptr(new c_module(*desc_, std::move(owned)), module_deleter{});
    }

    /// Answered from the descriptors, creating nothing: the C path declares its ports statically by
    /// design, so everything asked for here is already in the struct the plugin handed over.
    ///
    /// The defaults and the options are re-parsed rather than copied through as text. A descriptor
    /// whose default does not parse makes the module unusable at creation, and a description that
    /// stayed silent about it would show a healthy module in the palette that fails the moment it is
    /// placed. Both failures therefore carry the same wording as the ones make_property raises.
    [[nodiscard]] module_declaration declaration() const override {
        module_declaration decl;
        for (std::uint32_t i = 0; i < desc_->input_count; ++i) {
            const atp_input_desc& d = desc_->inputs[i];
            decl.inputs.push_back({std::string(detail::c_text(d.name)), detail::with_c_kind(d.kind, [](auto tag) {
                                       return std::type_index(typeid(typename decltype(tag)::type));
                                   })});
        }
        for (std::uint32_t i = 0; i < desc_->output_count; ++i) {
            const atp_output_desc& d = desc_->outputs[i];
            decl.outputs.push_back({std::string(detail::c_text(d.name)), detail::with_c_kind(d.kind, [](auto tag) {
                                        return std::type_index(typeid(typename decltype(tag)::type));
                                    })});
        }
        for (std::uint32_t i = 0; i < desc_->property_count; ++i) {
            decl.properties.push_back(declare_property(desc_->properties[i]));
        }
        return decl;
    }

   private:
    /// Describes one property, refusing exactly what make_property refuses and with the same text.
    ///
    /// The blob branch below is unreachable and has to exist anyway: with_c_kind instantiates every
    /// kind whatever the descriptor asks for, and the guard in front of the dispatch is what keeps the
    /// branch unreachable — the same shape as c_module::build_properties.
    [[nodiscard]] property_declaration declare_property(const atp_property_desc& d) const {
        std::string name{detail::c_text(d.name)};
        if (d.kind == ATP_KIND_BLOB) {
            throw std::runtime_error("module '" + name_ + "': property '" + name + "' cannot be a blob");
        }
        return detail::with_c_kind(d.kind, [&](auto tag) {
            using payload = decltype(tag)::type;
            if constexpr (io::property_value<payload>) {
                if (!io::property_codec<payload>::from_string(detail::c_text(d.default_value))) {
                    throw std::invalid_argument("module '" + name_ + "': property '" + name +
                                                "' has an unparsable default '" +
                                                std::string(detail::c_text(d.default_value)) + "'");
                }
                std::vector<std::string> options;
                if (d.options != nullptr) {
                    options.reserve(d.option_count);
                    for (std::uint32_t i = 0; i < d.option_count; ++i) {
                        if (!io::property_codec<payload>::from_string(detail::c_text(d.options[i]))) {
                            throw std::invalid_argument("module '" + name_ + "': property '" + name +
                                                        "' has an unparsable option '" +
                                                        std::string(detail::c_text(d.options[i])) + "'");
                        }
                        options.emplace_back(detail::c_text(d.options[i]));
                    }
                }
                return property_declaration{std::move(name), io::property_codec<payload>::kind,
                                            std::string(detail::c_text(d.default_value)), std::move(options),
                                            d.persistent != 0};
            } else {
                return property_declaration{std::move(name), io::property_kind::text, {}, {}, true};
            }
        });
    }

    const atp_module_desc* desc_;
    std::string name_;
    version version_;
};

/// What one module registered as, and the file its descriptor pointed at.
///
/// The source travels beside the registration rather than inside the factory: a factory is a plugin
/// ABI type and cannot grow a field, while this list never leaves the host.
struct c_registration {
    std::string name;
    version ver;
    std::string source;
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
/// @return one entry per registered module, in registration order
/// @throws std::runtime_error on an ABI mismatch, a null descriptor or a malformed one
inline std::vector<c_registration> register_c_modules(module_registrar& registrar,
                                                      c_abi_version_fn* abi,
                                                      c_module_count_fn* count,
                                                      c_module_desc_at_fn* at) {
    if (const unsigned plugin_version = abi(); plugin_version != ATP_C_ABI) {
        throw std::runtime_error("plugin has C ABI " + std::to_string(plugin_version) + ", host expects " +
                                 std::to_string(ATP_C_ABI));
    }
    std::vector<c_registration> made;
    const unsigned total = count();
    for (unsigned i = 0; i < total; ++i) {
        const atp_module_desc* desc = at(i);
        if (desc == nullptr) {
            throw std::runtime_error("module descriptor " + std::to_string(i) + " is null");
        }
        const module_factory_base& added = registrar.add(std::make_unique<c_module_factory>(*desc));
        made.push_back({std::string(added.name()), added.get_version(), std::string(detail::c_desc_source(*desc))});
    }
    return made;
}

}  // namespace atp::runtime

#endif
