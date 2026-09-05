// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_MODULE_CONFIG_HPP
#define ANITOOLSPLATFORM_MODULE_MODULE_CONFIG_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/config/access_error.hpp>
#include <atp/config/scalar.hpp>
#include <atp/io/enum_names.hpp>
#include <atp/io/option_set.hpp>
#include <atp/io/property_codec.hpp>

namespace atp {

/// What a declared field holds. The four scalars are the forms a config value can take; object and
/// array are the two shapes group() and list() produce.
enum class field_kind { boolean, integer, real, string, object, array };

/// What a field can hold directly: one of the four scalar forms, or an enumeration with a name table.
///
/// An enumeration is **not** a seventh kind. Its kind is string, exactly as property_codec<TEnum>
/// answers property_kind::text, and what makes it an enumeration is a non-empty options() — the same
/// rule properties keep, so that a module's config and its properties do not speak of the same thing
/// in two ways. In a document an enumeration is therefore a name and nothing else, which is also what
/// lets it cross the C boundary and reach both script bridges with no vocabulary of its own.
template <typename T>
concept field_value = config::scalar<T> || io::named_enum<T>;

class module_config;

/// A field declared from data rather than from a type: what a host fills in when it describes a module
/// it did not compile.
///
/// Nested objects are deliberately not declarable through it — an object needs a child config of a type
/// this header does not know, and module_config::group<T>() already says that with the type doing the
/// talking. dynamic_config::object() is the door.
struct dynamic_field {
    std::string name;

    /// One of the four scalar forms, or field_kind::array. field_kind::object is refused.
    field_kind kind = field_kind::string;

    /// Array only: the form of one element. Never field_kind::array — an array of arrays is not
    /// declarable, the same limit the typed declarators have.
    field_kind element = field_kind::string;

    /// The default in canonical string form, or nothing at all, which declares the field required.
    /// Unread for an array.
    std::optional<std::string> fallback;

    /// Canonical strings the field accepts — for an array, what every element accepts. Empty means
    /// anything of its form.
    std::vector<std::string> options;

    /// Builds one fresh element of an array of objects, at its declared defaults. Null for every other
    /// form. Called once at the declaration to make the prototype element_shape() answers, and again
    /// for every element the array grows by — which is what makes "what an element would be" and "what
    /// an element is" the same answer by construction.
    std::function<std::shared_ptr<module_config>()> make_element;
};

/// A module's declared config: the fields it names, the values behind them, and a way for a host to
/// walk and edit both.
///
/// An heir declares its fields as reference members, exactly as an io section declares ports:
///
///     struct scaler_config : atp::module_config {
///         using module_config::module_config;
///         double& gain = field("gain", 1.0);
///         std::string& device = field<std::string>("device");
///     };
///
/// **It knows nothing about the config document.** No node, no parser, no path grammar: an object of
/// this class is a declaration with values in it, and filling one from a document is a host-side job
/// done through entries(). That is the whole point of the split — the type a module names in its own
/// source drags no document library across the plugin ABI, and a host can describe and edit a config
/// whose module it never built.
///
/// Usable on its own and not only as a base: `using config_type = atp::module_config;` is how a module
/// says it takes a config with no declared fields at all, and every operation here answers sensibly on
/// an object with no entries.
///
/// Neither copyable nor movable, and it cannot become either: an heir binds references into this
/// object's own storage, so the object has to stay where it was built. That is also why every value
/// container here is a deque — a vector would rehome its elements on growth and leave those references
/// dangling.
///
/// Not thread-safe and not meant to be: fields are declared in a constructor and edited by whoever owns
/// the object.
class module_config {
   public:
    /// One declared field, as a host sees it without knowing the module's type.
    ///
    /// Holds nothing of its own but its name and its declaration: the value lives in the owner's
    /// storage and is reached through a pointer, so an entry is a small copyable record that can be
    /// moved around inside entries_ while the heir's references stay where they are.
    class entry {
       public:
        [[nodiscard]] std::string_view name() const noexcept {
            return name_;
        }

        [[nodiscard]] field_kind kind() const noexcept {
            return kind_;
        }

        /// What one element of an array holds — one of the four scalars, or object for an array of
        /// objects, whose shape element_shape() describes. Meaningless for every other kind.
        ///
        /// It has to be recorded rather than inferred: an editor drawing a row for an element of an
        /// empty array has nothing else to learn from what to parse it as.
        [[nodiscard]] field_kind element() const noexcept {
            return element_;
        }

        /// Whether the field was declared without a default, so its absence from a document is a
        /// problem for whoever fills this config rather than something to fall back from.
        [[nodiscard]] bool required() const noexcept {
            return required_;
        }

        /// Whether anybody wrote this field **through an entry**.
        ///
        /// Not decoration: without it a required field nobody filled is indistinguishable from one
        /// filled with the zero of its type, and a host saving the config back into a document would
        /// write 0 for it — which makes the error "required and absent" impossible to express.
        ///
        /// The other half of the contract is that writing through the **bound reference**
        /// (`cfg.gain = 2.0`) does not raise the flag; only writing through an entry does. That is
        /// right for every flow the platform has — the loader and the editor both write through
        /// entries, while a module's own writes to its own config are its business and not the
        /// document's — and it is the reason a host must never edit through references and then expect
        /// to save what it changed.
        [[nodiscard]] bool is_set() const noexcept {
            return set_;
        }

        /// @throws config::access_error naming the field, the form it holds and the form asked for
        template <field_value T>
        [[nodiscard]] const T& value() const {
            require_value_type<T>();
            return *static_cast<const T*>(value_);
        }

        /// Writes the field and marks it set.
        ///
        /// For the string kind this is **not** the door a host uses: an enumeration holds a value of
        /// its own type rather than a name, so set(std::string) on one throws, and from_string() is
        /// the call that both parses the name and checks the declared set. It also answers false
        /// instead of throwing, which is what lets load_fields report a problem line.
        /// @throws config::access_error naming the field, the form it holds and the form asked for, or
        ///         saying that the value is outside the declared set
        template <field_value T>
        void set(T value) {
            require_value_type<T>();
            if (!options_.empty()) {
                require_allowed(io::property_codec<T>::to_string(value));
            }
            *static_cast<T*>(value_) = std::move(value);
            set_ = true;
        }

        /// Values this field accepts, in declaration order; empty when it accepts anything of its form.
        ///
        /// Non-empty is what makes a field an enumeration — the type-level name table of an enum, or
        /// the set listed at the declaration with allowed(), which **replaces** the table rather than
        /// extending it. That is how a module narrows an enum down to the subset it supports.
        [[nodiscard]] const std::vector<std::string>& options() const noexcept {
            return options_;
        }

        /// The value as text, in the one string vocabulary the platform has — io::property_codec, the
        /// very codec a property is read and written through. A second implementation of the same
        /// conversion is how "1.0" and "1" start to differ depending on which layer printed them.
        /// @throws config::access_error when the field is an object or an array, which have no string
        ///         form
        [[nodiscard]] std::string to_string() const {
            if (text_ops_ == nullptr) {
                throw config::access_error(no_string_form());
            }
            return text_ops_->to_string(value_);
        }

        /// Parses @p text into the field, leaving the value alone when it does not parse.
        ///
        /// The two failures are deliberately different: text that does not parse is data and answers
        /// false, while asking an object or an array for a string form is a mistake in the caller's own
        /// source and throws.
        /// @return whether the text parsed, in which case the field is written and marked set
        /// @throws config::access_error when the field is an object or an array
        bool from_string(std::string_view text) {
            if (text_ops_ == nullptr) {
                throw config::access_error(no_string_form());
            }
            if (!allows(text) || !text_ops_->from_string(value_, text)) {
                return false;
            }
            set_ = true;
            return true;
        }

        /// The declared default, as text; empty for a required field and for an object or an array.
        ///
        /// The default is kept as text rather than as a second typed value on purpose: both sides of
        /// is_default() then go through the same io::property_codec, so a default written 1.0 and a
        /// value printed "1" cannot drift apart, and no storage has to exist for a form the field does
        /// not hold.
        [[nodiscard]] std::string default_string() const {
            return default_text_;
        }

        /// Whether the field still holds what it was declared with.
        ///
        /// False for a required field, which has no default to be at, and for an object or an array,
        /// which are described by their contents rather than by a value.
        [[nodiscard]] bool is_default() const {
            if (required_ || !is_scalar()) {
                return false;
            }
            return to_string() == default_text_;
        }

        /// The nested config behind an object field.
        ///
        /// Const only in that it does not change the entry: the config it hands back is the owner's and
        /// is meant to be edited, exactly as a pointer read from a const record still points at mutable
        /// storage.
        /// @throws config::access_error when the field is not an object
        [[nodiscard]] module_config& group() const {
            if (kind_ != field_kind::object || value_ == nullptr) {
                throw config::access_error(is_not("an object"));
            }
            return *static_cast<module_config*>(value_);
        }

        /// @throws config::access_error when the field is not an array
        [[nodiscard]] std::size_t size() const {
            require_list();
            return ops_->size(value_);
        }

        /// Grows the array with fresh elements — each at its own declared defaults, or at the zero of
        /// its kind for an array of scalars — or drops elements off the tail.
        ///
        /// **Not std::deque::resize**: that one requires MoveInsertable, and an heir of module_config
        /// is neither movable nor copyable, so an array of objects has to grow with emplace_back() and
        /// shrink with pop_back(). See list_ops::resize; do not simplify it back.
        /// @throws config::access_error when the field is not an array
        void resize(std::size_t n) {
            require_list();
            ops_->resize(value_, n);
            set_ = true;
        }

        /// Element @p i of an array of objects.
        /// @throws config::access_error when the field is not an array of objects, or @p i is past the
        ///         end
        [[nodiscard]] module_config& group_at(std::size_t i) const {
            require_list();
            if (ops_->at == nullptr) {
                throw config::access_error(is_not("an array of objects"));
            }
            require_index(i);
            return ops_->at(value_, i);
        }

        /// What one element of an array of objects looks like: a prototype holding the element type's
        /// own declarations at their defaults.
        ///
        /// It exists so that an **empty** array still describes itself — an editor offering to add an
        /// element has to know what an element is before there is one to look at.
        /// @throws config::access_error when the field is not an array of objects
        [[nodiscard]] const module_config& element_shape() const {
            require_list();
            if (shape_ == nullptr) {
                throw config::access_error(is_not("an array of objects"));
            }
            return *shape_;
        }

        /// The elements of an array of scalars, to be read and written as they are.
        ///
        /// Writing here does not mark the field set, exactly as writing through a bound reference does
        /// not: the container is the module's own storage, and no write to it passes through an entry
        /// that could record it. A host that means the edit to be saved goes through
        /// set_element_from_string() or resize().
        /// @throws config::access_error when the field is not an array of @p T
        template <field_value T>
        [[nodiscard]] std::deque<T>& values() const {
            require_list();
            if (element_ != kind_of<T>() || type_ != std::type_index(typeid(T))) {
                throw config::access_error(is_not("an array of " + std::string(kind_name(kind_of<T>()))));
            }
            return *static_cast<std::deque<T>*>(value_);
        }

        /// Element @p i of an array of scalars, as text.
        /// @throws config::access_error when the field is not an array of scalars, or @p i is past the
        ///         end
        [[nodiscard]] std::string element_string(std::size_t i) const {
            require_list();
            if (ops_->to_string == nullptr) {
                throw config::access_error(is_not("an array of scalars"));
            }
            require_index(i);
            return ops_->to_string(value_, i);
        }

        /// Parses @p text into element @p i of an array of scalars, leaving it alone when it does not
        /// parse.
        /// @return whether the text parsed, in which case the field is marked set
        /// @throws config::access_error when the field is not an array of scalars, or @p i is past the
        ///         end
        bool set_element_from_string(std::size_t i, std::string_view text) {
            require_list();
            if (ops_->from_string == nullptr) {
                throw config::access_error(is_not("an array of scalars"));
            }
            require_index(i);
            if (!allows(text) || !ops_->from_string(value_, i, text)) {
                return false;
            }
            set_ = true;
            return true;
        }

       private:
        friend class module_config;

        /// Type-erased handle on a declared array. Function pointers rather than virtuals: the element
        /// type is known only inside list<T>(), and a table of small captureless lambdas costs neither
        /// an allocation nor a vtable, while an entry stays a copyable record.
        ///
        /// Which half is null says what kind of array this is — `at` for an array of objects, the two
        /// string operations for an array of scalars — so the entry needs no second discriminator.
        struct list_ops {
            std::size_t (*size)(void*);

            /// Grows and shrinks the array. **std::deque::resize cannot be used for an array of
            /// objects**: it requires MoveInsertable, and an heir of module_config is neither movable
            /// nor copyable, so the implementation grows with emplace_back() and shrinks with
            /// pop_back(). That is the reason for the loops there; do not simplify them back.
            void (*resize)(void*, std::size_t);

            /// Element as a config, or null for an array of scalars.
            module_config& (*at)(void*, std::size_t);

            /// Element as text, or null for an array of objects.
            std::string (*to_string)(void*, std::size_t);

            /// Element parsed from text, or null for an array of objects.
            bool (*from_string)(void*, std::size_t, std::string_view);
        };

        /// The two halves of a scalar's string form, bound to the value's own codec.
        struct scalar_ops {
            std::string (*to_string)(const void*);
            bool (*from_string)(void*, std::string_view);
        };

        template <field_value T>
        [[nodiscard]] static constexpr field_kind kind_of() noexcept {
            if constexpr (std::same_as<T, bool>) {
                return field_kind::boolean;
            } else if constexpr (std::same_as<T, std::int64_t>) {
                return field_kind::integer;
            } else if constexpr (std::same_as<T, double>) {
                return field_kind::real;
            } else {
                return field_kind::string;
            }
        }

        [[nodiscard]] static constexpr std::string_view kind_name(field_kind kind) noexcept {
            switch (kind) {
                case field_kind::boolean:
                    return "boolean";
                case field_kind::integer:
                    return "integer";
                case field_kind::real:
                    return "real";
                case field_kind::string:
                    return "string";
                case field_kind::object:
                    return "object";
                case field_kind::array:
                    return "array";
            }
            return "unknown";
        }

        [[nodiscard]] bool is_scalar() const noexcept {
            return kind_ != field_kind::object && kind_ != field_kind::array;
        }

        void require_kind(field_kind wanted) const {
            if (kind_ != wanted) {
                throw config::access_error(is_not(std::string("a ") + std::string(kind_name(wanted))));
            }
        }

        /// The kind alone stopped being enough when enumerations arrived: two enums and a plain string
        /// all answer field_kind::string, and casting the storage of one to another would reinterpret an
        /// object of a different size. The recorded type is what tells them apart, the same guard
        /// property_base keeps with its own typeid and service_directory with type_index equality.
        template <field_value T>
        void require_value_type() const {
            require_kind(kind_of<T>());
            if (type_ != std::type_index(typeid(T))) {
                throw config::access_error(name_ + ": is declared " + type_.name() + ", not " + typeid(T).name());
            }
        }

        [[nodiscard]] bool allows(std::string_view text) const noexcept {
            return options_.empty() || std::ranges::find(options_, text) != options_.end();
        }

        void require_allowed(const std::string& text) const {
            if (allows(text)) {
                return;
            }
            std::string wanted;
            for (const std::string& option : options_) {
                if (!wanted.empty()) {
                    wanted += '|';
                }
                wanted += option;
            }
            throw config::access_error(name_ + ": '" + text + "' is not one of " + wanted);
        }

        void require_list() const {
            if (kind_ != field_kind::array || ops_ == nullptr) {
                throw config::access_error(is_not("an array"));
            }
        }

        void require_index(std::size_t i) const {
            const std::size_t count = ops_->size(value_);
            if (i >= count) {
                throw config::access_error(name_ + ": has no element " + std::to_string(i) + " (size " +
                                           std::to_string(count) + ")");
            }
        }

        [[nodiscard]] std::string is_not(const std::string& wanted) const {
            return name_ + ": is declared " + std::string(kind_name(kind_)) + ", not " + wanted;
        }

        [[nodiscard]] std::string no_string_form() const {
            return name_ + ": is declared " + std::string(kind_name(kind_)) + ", which has no string form";
        }

        std::string name_;
        field_kind kind_ = field_kind::string;
        field_kind element_ = field_kind::string;
        bool required_ = false;
        bool set_ = false;

        /// The declared default as text; see default_string() for why it is not a second typed value.
        std::string default_text_;

        /// Where the value lives in the owner's storage: an element of one of the four deques for a
        /// scalar, the nested module_config for a group, the deque itself for a list. All of them are
        /// stable addresses, which is what makes an entry safe to move around inside entries_.
        void* value_ = nullptr;

        /// Prototype of one element of an array of objects; null for everything else.
        module_config* shape_ = nullptr;

        /// Type of the value behind value_ — of one element for an array of scalars. typeid(void) for
        /// an object and for an array of objects, which are reached as module_config and never cast.
        std::type_index type_ = std::type_index(typeid(void));

        /// Values this field accepts; empty means anything of its form. See options().
        std::vector<std::string> options_;

        const list_ops* ops_ = nullptr;

        /// Text conversion of the value, taken from io::property_codec at the declaration.
        ///
        /// Function pointers rather than a switch on kind_, for the reason the type had to be recorded
        /// beside the kind: an enumeration's kind is string, so the kind no longer says which codec
        /// prints the value. Null for an object and an array, which have no string form. Captureless
        /// lambdas over a static, exactly like list_ops — an entry stays a copyable record with no
        /// allocation and no vtable.
        const scalar_ops* text_ops_ = nullptr;
    };

    module_config() = default;
    module_config(const module_config&) = delete;
    module_config& operator=(const module_config&) = delete;

    /// Virtual because a config is owned as a config_ptr and destroyed by a host that knows nothing of
    /// the heir — and, when the heir came from a plugin, on the far side of a library boundary.
    virtual ~module_config() = default;

    /// Declared fields, in declaration order — the same contract the io registries keep: what the
    /// author wrote, in the order they wrote it.
    ///
    /// The records live in a vector, unlike every other container here, because nothing binds a
    /// reference to them: an heir's references point into the value storage, not into an entry. A
    /// pointer into this span stays valid until the next declaration, which for an heir of this class
    /// means "once the constructor is over" and for dynamic_config means "once the host has finished
    /// declaring into it" — c_config declares from data after construction and hands the object out
    /// only afterwards.
    [[nodiscard]] std::span<entry> entries() noexcept {
        return entries_;
    }

    [[nodiscard]] std::span<const entry> entries() const noexcept {
        return entries_;
    }

    /// Entry named @p name, or nullptr when this config declares no such field. A linear search, like
    /// the io registries and for the same reason: declaration order is worth more than a lookup that
    /// happens at load time and on an edit.
    [[nodiscard]] const entry* find(std::string_view name) const noexcept {
        for (const entry& e : entries_) {
            if (e.name() == name) {
                return &e;
            }
        }
        return nullptr;
    }

    [[nodiscard]] entry* find(std::string_view name) noexcept {
        return const_cast<entry*>(std::as_const(*this).find(name));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
    }

    /// Bytes of the file this config was read from, verbatim and including a BOM; empty when it did not
    /// come from a file. Declared UTF-8 like every other string crossing an ABI here, and neither
    /// validated nor transcoded.
    [[nodiscard]] const std::string& text() const noexcept {
        return text_;
    }

    /// Path of that file, so a module parsing the text itself can say "rig.yaml:12: ..." and can
    /// resolve paths written *inside* its config against it.
    ///
    /// Named origin rather than source because module_info::source already means the file a module is
    /// *declared* in, which is a different file.
    [[nodiscard]] const std::string& origin() const noexcept {
        return origin_;
    }

    /// Whether the text is all there is. Not derivable from the rest: a file of a format the host does
    /// parse can hold literally `null` and leave every field at its default beside a non-empty text, and
    /// a module deciding whether to parse the text itself would get that case wrong.
    [[nodiscard]] bool is_opaque() const noexcept {
        return opaque_;
    }

    /// Attaches the bytes this config was read from.
    ///
    /// Separate from filling the fields because the two are done by different people: the fields by
    /// whoever walks the document, the source by whoever read the file. A config that came from no
    /// file simply never has this called on it. Public rather than protected because the caller is
    /// the host — runtime::load_fields and raw_config::adopt — and neither can be named from this
    /// header without dragging the runtime into the SDK; a module calling it on its own config
    /// rewrites only what it is about to read.
    /// @param text bytes of the file, verbatim
    /// @param origin path of that file, named in the module's own messages
    /// @param opaque whether the text is all there is
    void attach_source(std::string text, std::string origin, bool opaque) {
        text_ = std::move(text);
        origin_ = std::move(origin);
        opaque_ = opaque;
    }

   protected:
    /// Declares an optional field — one of the four scalar forms, or an enumeration, which is a value
    /// of its own type here and not its name:
    ///
    ///     double& gain = field("gain", 1.0);
    ///     channel_layout& layout = field("layout", channel_layout::stereo);
    ///
    /// An enum's declared options come from its name table, so the second line needs nothing else; a
    /// module supporting only part of the table lists it with the overload below.
    /// @param name key within this object
    /// @param fallback value the field holds until somebody writes it
    /// @throws config::access_error if the fallback is outside the type's own value set
    template <field_value T>
    T& field(std::string name, T fallback) {
        return declare_value<T>(std::move(name), std::move(fallback), false, io::detail::type_options<T>());
    }

    /// Declares an optional field with a value set listed at the declaration, the same vocabulary a
    /// property uses:
    ///
    ///     std::int64_t& channels = field("channels", std::int64_t{2}, io::allowed(1, 2, 6));
    ///     channel_layout& layout = field("layout", channel_layout::mono,
    ///                                    io::allowed(channel_layout::mono, channel_layout::stereo));
    ///
    /// For an enum the listed set **replaces** the type's name table rather than extending it — that is
    /// how a module narrows an enumeration to what it actually supports. For an ordinary scalar it is
    /// the whole of what makes the field an enumeration.
    /// @param name key within this object
    /// @param fallback value the field holds until somebody writes it
    /// @param allowed the values it accepts, in the order they will be offered
    /// @throws config::access_error if the fallback is outside that set
    template <field_value T, typename TValue>
    T& field(std::string name, T fallback, const io::option_set<TValue>& allowed) {
        return declare_value<T>(std::move(name), std::move(fallback), false, io::detail::render_options<T>(allowed));
    }

    /// Declares an optional string field written with a literal.
    ///
    /// Present because a literal is a const char*, which is neither one of the four scalar forms nor an
    /// enumeration: without it the ordinary spelling field("preset", "default") would not compile at
    /// all. Deliberately an overload rather than a wider concept — field_value says what a field can
    /// *hold*, and const char* is a way of writing a string down, not a form to store.
    /// @param name key within this object
    /// @param fallback value the field holds until somebody writes it
    std::string& field(std::string name, const char* fallback) {
        return declare_value<std::string>(std::move(name), std::string(fallback == nullptr ? "" : fallback), false, {});
    }

    /// Declares a required scalar field — one with no default, whose absence from a document is a
    /// problem rather than a fallback. The type has to be spelled out because there is no default to
    /// deduce it from, the same shape as make<std::string>("file", "") in an io section.
    /// @param name key within this object
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    template <field_value T>
    T& field(std::string name) {
        return declare_value<T>(std::move(name), T{}, true, io::detail::type_options<T>());
    }

    /// Declares a nested object, built at its own declared defaults.
    /// @param name key within this object
    template <std::derived_from<module_config> T>
    T& group(std::string name) {
        std::shared_ptr<T> child = std::make_shared<T>();
        T& ref = *child;
        owned_.push_back(std::move(child));

        entry e;
        e.name_ = std::move(name);
        e.kind_ = field_kind::object;
        e.value_ = static_cast<module_config*>(&ref);
        entries_.push_back(std::move(e));
        return ref;
    }

    /// Declares an array of nested objects, empty until somebody grows it.
    ///
    /// Beside the array itself this builds one spare element and keeps it as the entry's shape, which
    /// is what lets an empty array still describe what an element of it would be.
    /// @param name key within this object
    template <std::derived_from<module_config> T>
    std::deque<T>& list(std::string name) {
        static constexpr entry::list_ops ops{
            [](void* p) { return static_cast<std::deque<T>*>(p)->size(); },
            [](void* p, std::size_t n) {
                std::deque<T>& items = *static_cast<std::deque<T>*>(p);
                while (items.size() < n) {
                    items.emplace_back();
                }
                while (items.size() > n) {
                    items.pop_back();
                }
            },
            [](void* p, std::size_t i) -> module_config& { return (*static_cast<std::deque<T>*>(p))[i]; },
            nullptr,
            nullptr,
        };

        auto items = std::make_shared<std::deque<T>>();
        auto shape = std::make_shared<T>();
        std::deque<T>& ref = *items;
        module_config& shape_ref = *shape;
        owned_.push_back(std::move(items));
        owned_.push_back(std::move(shape));

        entry e;
        e.name_ = std::move(name);
        e.kind_ = field_kind::array;
        e.element_ = field_kind::object;
        e.value_ = &ref;
        e.shape_ = &shape_ref;
        e.ops_ = &ops;
        entries_.push_back(std::move(e));
        return ref;
    }

    /// Declares an array of scalars or of enumerations, empty until somebody grows it. A fresh element
    /// starts at the zero of its type — an array has no per-element default to declare, and for an
    /// enumeration that zero may be a value its name table does not list, exactly as for a required
    /// field. What the elements accept is the type's own set, and it guards every element the same way
    /// a scalar field is guarded.
    /// @param name key within this object
    template <field_value T>
    std::deque<T>& list(std::string name) {
        static constexpr entry::list_ops ops{
            [](void* p) { return static_cast<std::deque<T>*>(p)->size(); },
            [](void* p, std::size_t n) { static_cast<std::deque<T>*>(p)->resize(n); },
            nullptr,
            [](void* p, std::size_t i) {
                return io::property_codec<T>::to_string((*static_cast<std::deque<T>*>(p))[i]);
            },
            [](void* p, std::size_t i, std::string_view text) {
                std::optional<T> parsed = io::property_codec<T>::from_string(text);
                if (!parsed) {
                    return false;
                }
                (*static_cast<std::deque<T>*>(p))[i] = std::move(*parsed);
                return true;
            },
        };

        auto items = std::make_shared<std::deque<T>>();
        std::deque<T>& ref = *items;
        owned_.push_back(std::move(items));

        entry e;
        e.name_ = std::move(name);
        e.kind_ = field_kind::array;
        e.element_ = entry::kind_of<T>();
        e.type_ = std::type_index(typeid(T));
        e.options_ = io::detail::type_options<T>();
        e.value_ = &ref;
        e.ops_ = &ops;
        entries_.push_back(std::move(e));
        return ref;
    }

    /// Declares a field whose form, requiredness, value set and element construction are all decided at
    /// runtime — the one door for a config built from a description rather than from a type.
    ///
    /// A C++ author has no use for it: the field/group/list overloads above say the same thing with the
    /// type doing the talking, and say it better. It exists because a host describing a module it did
    /// not compile has no type to talk with, and because rebuilding the option check and the codec
    /// binding outside this class would duplicate the one invariant that matters — that the default
    /// passes the very check every later write passes.
    /// @param spec what the field is; see dynamic_field for which of its members each form reads
    /// @throws config::access_error if the form is field_kind::object, if an array's element form is
    ///         field_kind::array, if the fallback does not parse as the declared form, or if it is
    ///         outside the declared options
    void declare(dynamic_field spec) {
        switch (spec.kind) {
            case field_kind::boolean:
                declare_dynamic_scalar<bool>(spec);
                return;
            case field_kind::integer:
                declare_dynamic_scalar<std::int64_t>(spec);
                return;
            case field_kind::real:
                declare_dynamic_scalar<double>(spec);
                return;
            case field_kind::string:
                declare_dynamic_scalar<std::string>(spec);
                return;
            case field_kind::array:
                declare_dynamic_array(spec);
                return;
            case field_kind::object:
                break;
        }
        throw config::access_error(spec.name + ": a nested object is declared with group<T>()");
    }

   private:
    /// Storage of an array of objects whose element type is known only at runtime.
    ///
    /// list<T>() grows with emplace_back() inside a captureless lambda, and an element described by data
    /// cannot be built that way. Keeping the factory beside the elements puts it back within reach of a
    /// lambda that captures nothing, so one static ops table serves every such array. The object itself
    /// lives in owned_, like every other type-erased declaration storage, so this adds no data member.
    struct element_array {
        std::deque<std::shared_ptr<module_config>> items;
        std::function<std::shared_ptr<module_config>()> make;
    };

    template <field_value T>
    void declare_dynamic_scalar(dynamic_field& spec) {
        const std::string where = spec.name;
        if (!spec.fallback) {
            declare_value<T>(std::move(spec.name), T{}, true, std::move(spec.options));
            return;
        }
        std::optional<T> parsed = io::property_codec<T>::from_string(*spec.fallback);
        if (!parsed) {
            throw config::access_error(where + ": default '" + *spec.fallback + "' is not a " +
                                       std::string(entry::kind_name(spec.kind)));
        }
        declare_value<T>(std::move(spec.name), std::move(*parsed), false, std::move(spec.options));
    }

    template <field_value T>
    void declare_dynamic_scalar_list(dynamic_field& spec) {
        list<T>(std::move(spec.name));
        entries_.back().options_ = std::move(spec.options);
    }

    void declare_dynamic_array(dynamic_field& spec) {
        switch (spec.element) {
            case field_kind::boolean:
                declare_dynamic_scalar_list<bool>(spec);
                return;
            case field_kind::integer:
                declare_dynamic_scalar_list<std::int64_t>(spec);
                return;
            case field_kind::real:
                declare_dynamic_scalar_list<double>(spec);
                return;
            case field_kind::string:
                declare_dynamic_scalar_list<std::string>(spec);
                return;
            case field_kind::object:
                declare_dynamic_object_array(spec);
                return;
            case field_kind::array:
                break;
        }
        throw config::access_error(spec.name + ": an array of arrays is not declarable");
    }

    void declare_dynamic_object_array(dynamic_field& spec) {
        static constexpr entry::list_ops ops{
            [](void* p) { return static_cast<element_array*>(p)->items.size(); },
            [](void* p, std::size_t n) {
                element_array& a = *static_cast<element_array*>(p);
                while (a.items.size() < n) {
                    a.items.push_back(a.make());
                }
                while (a.items.size() > n) {
                    a.items.pop_back();
                }
            },
            [](void* p, std::size_t i) -> module_config& { return *static_cast<element_array*>(p)->items[i]; },
            nullptr,
            nullptr,
        };

        auto held = std::make_shared<element_array>();
        held->make = std::move(spec.make_element);
        std::shared_ptr<module_config> shape = held->make();
        element_array& ref = *held;
        module_config& shape_ref = *shape;
        owned_.push_back(std::move(held));
        owned_.push_back(std::move(shape));

        entry e;
        e.name_ = std::move(spec.name);
        e.kind_ = field_kind::array;
        e.element_ = field_kind::object;
        e.value_ = &ref;
        e.shape_ = &shape_ref;
        e.ops_ = &ops;
        entries_.push_back(std::move(e));
    }

    template <field_value T>
    T& declare_value(std::string name, T fallback, bool required, std::vector<std::string> options) {
        static constexpr entry::scalar_ops ops{
            [](const void* p) { return io::property_codec<T>::to_string(*static_cast<const T*>(p)); },
            [](void* p, std::string_view text) {
                std::optional<T> parsed = io::property_codec<T>::from_string(text);
                if (!parsed) {
                    return false;
                }
                *static_cast<T*>(p) = std::move(*parsed);
                return true;
            },
        };

        entry e;
        e.name_ = std::move(name);
        e.kind_ = entry::kind_of<T>();
        e.required_ = required;
        e.type_ = std::type_index(typeid(T));
        e.options_ = std::move(options);
        e.text_ops_ = &ops;
        if (!required) {
            e.default_text_ = io::property_codec<T>::to_string(fallback);
            e.require_allowed(e.default_text_);
        }
        T& slot = store<T>(std::move(fallback));
        e.value_ = &slot;
        entries_.push_back(std::move(e));
        return slot;
    }

    /// Where a declared value lives.
    ///
    /// One deque per scalar form, and for an enumeration a slot of its own in owned_: the set of enum
    /// types is open, so no container of a fixed type can hold them, and this is the same type-erased
    /// storage a nested group and a list already use. It is also why the four deques were not folded
    /// into one deque of a variant — a variant would have to name every form a field can hold, and that
    /// list has no end.
    template <field_value T>
    T& store(T value) {
        if constexpr (io::named_enum<T>) {
            auto held = std::make_shared<T>(std::move(value));
            T& ref = *held;
            owned_.push_back(std::move(held));
            return ref;
        } else if constexpr (std::same_as<T, bool>) {
            bools_.push_back(value);
            return bools_.back();
        } else if constexpr (std::same_as<T, std::int64_t>) {
            ints_.push_back(value);
            return ints_.back();
        } else if constexpr (std::same_as<T, double>) {
            reals_.push_back(value);
            return reals_.back();
        } else {
            strings_.push_back(std::move(value));
            return strings_.back();
        }
    }

    std::vector<entry> entries_;

    /// One deque per scalar form, holding the values the heir's references are bound to. Deques rather
    /// than vectors because a vector would rehome its elements on growth and leave every reference
    /// declared before it dangling.
    std::deque<bool> bools_;
    std::deque<std::int64_t> ints_;
    std::deque<double> reals_;
    std::deque<std::string> strings_;

    /// Nested configs, list prototypes and lists, kept alive with their type erased: group() and list()
    /// are templates, so their storage cannot be a member of a fixed type. shared_ptr<void> carries the
    /// right deleter on its own, which a unique_ptr<void> would need spelled out by hand. A vector is
    /// enough here because what moves on growth is the pointers, not what they point at.
    std::vector<std::shared_ptr<void>> owned_;

    std::string text_;
    std::string origin_;
    bool opaque_ = false;
};

/// Config deleter carrying a pin, the twin of module_deleter: the object was built by the plugin's
/// code, its virtual destructor lives inside that library, and a schema prototype cached by a palette
/// outlives a rescan. For a module built into the host the pin is empty and this is a plain delete.
struct config_deleter {
    std::shared_ptr<void> pin;

    void operator()(module_config* cfg) const noexcept {
        delete cfg;
    }
};

/// Owning pointer to a module config.
using config_ptr = std::unique_ptr<module_config, config_deleter>;

}  // namespace atp

#endif
