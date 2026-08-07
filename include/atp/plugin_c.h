/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ANITOOLSPLATFORM_PLUGIN_C_H
#define ANITOOLSPLATFORM_PLUGIN_C_H

#include <stddef.h>
#include <stdint.h>

/// @file
/// Second registration path of the platform: a plugin that is not C++.
///
/// The C++ contract in plugin.hpp is shared header-only code with a version number attached — a
/// plugin compiles its own copies of the registries, instantiates its own io templates and throws
/// exceptions the host catches, which is why host and plugin must be one toolchain sharing one C++
/// runtime. Nothing about that is fixable without taking the SDK's ergonomics away from the C++
/// authors who are its point, so this file adds a path beside it rather than changing it.
///
/// The division of labour is the whole design: every C++ template, allocation and exception stays on
/// the host's side of this boundary. A plugin speaking this ABI contains no std type, frees nothing
/// the host allocated and instantiates no port — it publishes descriptions and function pointers, and
/// the host builds the real modules and ports from them. That is what lets a plugin be a Rust cdylib,
/// a Zig shared library or a bridge embedding an interpreter, with no C++ compiler involved at all,
/// and it is also why the atp_build_id() toolchain check of the C++ path is deliberately NOT applied
/// here: there is nothing left for a toolchain mismatch to corrupt.
///
/// The price is a closed set of port payload types (atp_kind). It buys the property that matters
/// more: a port declared here is an ordinary port of the platform, so a module written in another
/// language connects to a C++ one directly, with no adapter module in the config.
///
/// This header must keep compiling as C99. It is included by plugins that have no C++ compiler.

#if defined(_WIN32)
#define ATP_C_EXPORT __declspec(dllexport)
#else
#define ATP_C_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Version of this ABI, answered by atp_c_abi_version().
///
/// Independent of atp::plugin_abi, and expected to stay at 1 far longer: the C++ number counts
/// changes to a large shared surface, while everything here grows through the struct_size fields
/// instead. A bump means a change of meaning, not a new field.
#define ATP_C_ABI 1

/// Payload type of a port, and the one deliberate narrowing of this path.
///
/// Each value names a concrete C++ type the host instantiates the port with, so these are not a
/// parallel type system but six spellings inside the platform's own. ATP_KIND_BLOB is the escape
/// hatch for everything else: any payload a foreign module can serialise travels as bytes.
typedef enum atp_kind {
    ATP_KIND_I32 = 1,  /**< std::int32_t */
    ATP_KIND_I64 = 2,  /**< std::int64_t — spell the C++ peer's port std::int64_t too, not long */
    ATP_KIND_F64 = 3,  /**< double */
    ATP_KIND_BOOL = 4, /**< bool */
    ATP_KIND_TEXT = 5, /**< std::string */
    ATP_KIND_BLOB = 6  /**< atp::io::blob, that is std::vector<std::byte> */
} atp_kind;

/// Whether an input keeps the last value or queues them.
typedef enum atp_flavor {
    ATP_STATE = 0, /**< atp::io::input<T>: a later value displaces the one before it */
    ATP_QUEUE = 1  /**< atp::io::queued_input<T>: values accumulate up to a capacity */
} atp_flavor;

/// What a full queue does with a value that has nowhere to go; the platform's own overflow_policy.
typedef enum atp_overflow { ATP_DROP_OLDEST = 0, ATP_DROP_INCOMING = 1 } atp_overflow;

/// Severity of a log line, matching atp::log_level value for value.
typedef enum atp_log_level {
    ATP_LOG_ERROR = 0,
    ATP_LOG_WARNING = 1,
    ATP_LOG_INFO = 2,
    ATP_LOG_DEBUG = 3
} atp_log_level;

/// Outcome of a lifecycle call: zero is success, anything else a failure the module has described
/// through atp_api::set_error.
typedef int atp_status;

/// The success value of atp_status.
#define ATP_OK 0

/// Outcome of one pass of the hot path, mirroring atp::work_status with a failure added — a C
/// function cannot report one by throwing.
typedef enum atp_work {
    ATP_WORK_BUSY = 0,  /**< the module did or will do work; the runner keeps the pace */
    ATP_WORK_IDLE = 1,  /**< nothing to do; the runner may back off */
    ATP_WORK_ERROR = -1 /**< the pass failed, the reason left in set_error; stops the pipeline */
} atp_work;

/// One value crossing the boundary.
///
/// `kind` is redundant with the port's declared kind and is filled in regardless, because a wrong
/// pairing is the one mistake here that would otherwise be read as a valid value of another type:
/// the host checks it and refuses the call rather than reinterpreting the union.
typedef struct atp_value {
    atp_kind kind;
    union {
        int32_t i32;
        int64_t i64;
        double f64;
        int boolean;
        /// ATP_KIND_TEXT and ATP_KIND_BLOB. Text is not required to be NUL-terminated — `size` is
        /// the length — and may contain embedded NULs, since std::string can.
        struct {
            const char* data;
            size_t size;
        } bytes;
    } as;
} atp_value;

/// One module instance's side of the platform, opaque and owned by the host: the ports, the log, the
/// thread. Created before the module's own state and valid until after it is destroyed.
typedef struct atp_ctx atp_ctx;

/// Everything the host offers a module instance, handed over once at creation.
///
/// Ports are addressed by index into the arrays of the module descriptor — not by name — so that a
/// read on the hot path costs an array index rather than a hash lookup. An index out of range is
/// refused, never guessed.
///
/// Every callback here is noexcept on the host's side: whatever would have been an exception becomes
/// a zero or a non-zero return, so no C++ exception ever unwinds a foreign stack frame. The reverse
/// holds by construction — this side reports failures by return code — which makes the boundary
/// exception-free in both directions. A foreign runtime that unwinds on its own (a Rust panic) must
/// be stopped before it reaches here: `panic = "abort"`, or catch_unwind at each entry point.
///
/// New callbacks are appended and `struct_size` says how many are really there. A module that wants
/// one added after ATP_C_ABI 1 checks the size before reading the pointer.
typedef struct atp_api {
    /// sizeof(atp_api) as the host knows it.
    uint32_t struct_size;

    /// Reads the value of a state input without consuming it — atp::io::input<T>::get().
    /// @param i index into the descriptor's `inputs`
    /// @param out filled in only when the answer is 1
    /// @return 1 if the input held a value, 0 if it was empty or @p i is out of range
    int (*get_input)(atp_ctx* ctx, uint32_t i, atp_value* out);

    /// Removes and returns the next value of an input — atp::io::input<T>::take(), which for a
    /// queueing input is the FIFO head. The way to read an input whose every value must be handled
    /// exactly once.
    /// @param i index into the descriptor's `inputs`
    /// @param out filled in only when the answer is 1
    /// @return 1 if a value was taken, 0 if there was none or @p i is out of range
    int (*take_input)(atp_ctx* ctx, uint32_t i, atp_value* out);

    /// Writes a value to an output, delivering it to every connected input.
    /// @param i index into the descriptor's `outputs`
    /// @param value must carry the output's declared kind
    /// @return 1 on success, 0 if @p i is out of range or the kind does not match
    int (*write_output)(atp_ctx* ctx, uint32_t i, const atp_value* value);

    /// Reads a property value — atp::io::property<T>::get().
    /// @param i index into the descriptor's `properties`
    /// @return 1 on success, 0 if @p i is out of range
    int (*get_property)(atp_ctx* ctx, uint32_t i, atp_value* out);

    /// Reads a property value if it was written since the last such call, and clears that flag —
    /// atp::io::property<T>::take(). How a module reacts to a setting being edited while it runs.
    /// @param i index into the descriptor's `properties`
    /// @param out filled in only when the answer is 1
    /// @return 1 if the value had changed, 0 if it had not or @p i is out of range
    int (*take_property)(atp_ctx* ctx, uint32_t i, atp_value* out);

    /// Writes a property value, as a module editing its own setting.
    /// @param i index into the descriptor's `properties`
    /// @param value must carry the property's declared kind
    /// @return 1 on success, 0 if @p i is out of range, the kind does not match, or the value is
    ///         outside the property's declared options
    int (*set_property)(atp_ctx* ctx, uint32_t i, const atp_value* value);

    /// Whether a stop has been requested — what std::stop_token tells a C++ module's iterate.
    /// @return 1 once the pipeline is stopping
    int (*stop_requested)(atp_ctx* ctx);

    /// Writes one log line. Never blocks and never fails; a line that does not fit is dropped and
    /// counted. A no-op before the module is initialised and after it is stopped.
    /// @param text need not be NUL-terminated
    void (*log)(atp_ctx* ctx, atp_log_level level, const char* text, size_t len);

    /// Asks this module's thread to iterate now; callable from any thread. A wake-up carrying no
    /// value, and a no-op before start and after stop.
    void (*wake)(atp_ctx* ctx);

    /// Describes the failure a lifecycle call or a pass is about to report. The text is copied here
    /// and becomes the message of the error the host raises, so it should name what went wrong
    /// without repeating the module's name — the host adds that.
    /// @param text need not be NUL-terminated
    void (*set_error)(atp_ctx* ctx, const char* text, size_t len);
} atp_api;

/// Creates the module's own state.
///
/// The context exists already but has no host behind it yet: log and wake are no-ops until the
/// platform initialises the module, exactly as a C++ module has no module_context before
/// initialize(). Reading ports here is legal and answers empty — nothing is connected yet.
/// @param api valid for as long as the module lives; worth storing next to the state
/// @param ctx this instance's context, to be passed back to every api call
/// @param user_data the `user_data` of the descriptor this function came from, which is how one
///        create function serves many descriptors — a bridge generating a module per script
/// @return the module state, or NULL to refuse creation
typedef void* (*atp_create_fn)(const atp_api* api, atp_ctx* ctx, void* user_data);

/// Destroys the module state. Called exactly once, after stop, and cannot fail.
typedef void (*atp_destroy_fn)(void* self);

/// A lifecycle step: initialize, start or stop.
///
/// The contract of stop is the platform's, and does not follow from the signature: **it must be
/// correct after initialize without start**, because a pipeline whose start cascade fails rolls back
/// by stopping everything already initialised.
/// @return ATP_OK, or non-zero after describing the failure through set_error
typedef atp_status (*atp_lifecycle_fn)(void* self);

/// One pass of the hot path. Gets no arguments beyond the state by design — everything it needs is
/// the api and ctx it was created with.
typedef atp_work (*atp_iterate_fn)(void* self);

/// Declaration of one input.
typedef struct atp_input_desc {
    /// NUL-terminated, unique among this module's inputs.
    const char* name;
    atp_kind kind;
    atp_flavor flavor;
    /// ATP_QUEUE only: how many unread values the queue holds before `overflow` applies. Zero asks
    /// for the platform's own default limit rather than for "no limit" — an unbounded queue between
    /// threads of different pacing is how a pipeline runs out of memory with no error and no metric
    /// along the way. That default carries its own policy, so a zero here makes `overflow` unread.
    uint32_t capacity;
    /// ATP_QUEUE with a non-zero capacity only.
    atp_overflow overflow;
} atp_input_desc;

/// Declaration of one output.
typedef struct atp_output_desc {
    /// NUL-terminated, unique among this module's outputs.
    const char* name;
    atp_kind kind;
} atp_output_desc;

/// Declaration of one property: a setting with a default, edited live by the config, the CLI or
/// studio, and read by the module through get_property/take_property.
typedef struct atp_property_desc {
    /// NUL-terminated, unique among this module's properties.
    const char* name;
    /// ATP_KIND_BLOB is refused: a property is a scalar a human edits as text.
    atp_kind kind;
    /// Default in the platform's canonical string form ("2", "1.5", "true", or the text itself). It
    /// is parsed by the same code that parses a config value, so a malformed default is a load-time
    /// error rather than a surprise later.
    const char* default_value;
    /// Allowed values, in canonical string form — an enumeration. NULL leaves the property
    /// unconstrained; a non-empty set makes every write check against it, including the default.
    const char* const* options;
    uint32_t option_count;
    /// Non-zero if the value belongs in a saved project, zero if it lives only while the pipeline
    /// runs.
    int persistent;
} atp_property_desc;

/// Everything the host needs to offer one module: what it declares and how it is driven.
///
/// Ports are declared here, statically, rather than at initialisation, and that is a requirement
/// rather than a convenience: the host builds the tree by creating modules, applying properties and
/// connecting ports, and only then initialises anything. A port that did not exist before the first
/// connection could not be connected at all.
typedef struct atp_module_desc {
    /// sizeof(atp_module_desc) as the plugin knows it. The host reads no field beyond it, which is
    /// how this structure grows without a new ATP_C_ABI.
    uint32_t struct_size;

    /// Registration name, NUL-terminated. Two modules may share a name only with different versions.
    const char* name;
    /// major, minor, patch, build — only the first `version_count` are read.
    uint32_t version[4];
    uint32_t version_count;

    const atp_input_desc* inputs;
    uint32_t input_count;
    const atp_output_desc* outputs;
    uint32_t output_count;
    const atp_property_desc* properties;
    uint32_t property_count;

    /// Passed to `create` unchanged; the host never dereferences it.
    void* user_data;

    atp_create_fn create;
    atp_destroy_fn destroy;
    /// initialize, start and stop may be NULL, which means "nothing to do" — the common case for a
    /// module that only transforms values. iterate and the create/destroy pair are required.
    atp_lifecycle_fn initialize;
    atp_lifecycle_fn start;
    atp_iterate_fn iterate;
    atp_lifecycle_fn stop;
} atp_module_desc;

/// The three symbols a plugin of this path exports. They are pulled, not pushed: the host asks for
/// the descriptors instead of handing over a registration callback, so loading needs no function
/// pointer travelling into the plugin, cannot be reentered, and lets a plugin be nothing but static
/// data.
///
///     ATP_C_EXPORT unsigned atp_c_abi_version(void);
///     ATP_C_EXPORT unsigned atp_module_count(void);
///     ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index);
///
/// A plugin may export these beside the C++ pair of plugin.hpp; both paths then run and both sets of
/// modules are registered.

/// @return ATP_C_ABI as the plugin was built against. A mismatch with the host's is refused, and
///         this is the only call that is safe under one — plain C, no parameters, no types.
ATP_C_EXPORT unsigned atp_c_abi_version(void);

/// @return how many modules this plugin offers. Called once, before atp_module_desc_at; a bridge
///         that discovers its modules (scanning a directory of scripts, say) may do that work here.
ATP_C_EXPORT unsigned atp_module_count(void);

/// Describes one module.
/// @param index below the count answered by atp_module_count
/// @return a descriptor that must stay valid, with everything it points at, until the library is
///         unloaded — the host keeps the pointer for the lifetime of the registration. NULL is a
///         refusal and fails the load.
ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index);

/// Lifetime of a value crossing this boundary, which is the one rule worth reading twice. **No
/// allocation ever crosses: neither side frees what the other made.**
///
/// Host to module — the `out` of get_input, take_input, get_property and take_property: `bytes.data`
/// points into storage owned by the host and stays valid until the next call that yields a value on
/// the same ctx (one of those same four) or until iterate returns, whichever comes first. Writing an
/// output does not invalidate it, which is what makes the natural thing legal: read a payload and pass
/// it straight to write_output. Holding two payloads at once does not work — there is one buffer per
/// module instance, which is also why reading a port costs no allocation — so a module that needs the
/// bytes across another read copies them.
///
/// Module to host — the `value` of write_output and set_property: `bytes.data` need only stay valid
/// for the duration of the call. The host copies what it keeps. This is the same contract the io
/// layer already gives its own writers, where a delivered value is the writer's own object and every
/// input copies out of it.

#ifdef __cplusplus
}
#endif

#endif
