// SPDX-License-Identifier: Apache-2.0
#![allow(non_camel_case_types)]
// The mirror carries every constant the header declares, whether this plugin happens to use it or
// not: a partial mirror is what makes the next author add one by hand and get it wrong. Completeness
// is the point of the file, so the unused half of it is not a warning here.
#![allow(dead_code)]

//! Rust mirror of `include/atp/plugin_c.h`.
//!
//! Hand-written rather than generated: bindgen would work, but it makes a C toolchain and a build
//! script a dependency of every plugin, which is the opposite of what this path is for — the point is
//! that a Rust plugin needs nothing but cargo. The cost is that this file has to be kept in step with
//! the header by hand.
//!
//! What that costs, precisely: `struct_size` lets the host detect a struct that is *shorter* than it
//! expects, so a field added upstream and not added here is caught at load time with a clear message.
//! A field *reordered* or retyped is not caught by anything, so read the header when `ATP_C_ABI`
//! changes rather than trusting this file.

use core::ffi::{c_char, c_int, c_void};

/// The ABI this file mirrors. The host refuses anything else.
pub const ATP_C_ABI: u32 = 1;

pub const ATP_KIND_I32: c_int = 1;
pub const ATP_KIND_I64: c_int = 2;
pub const ATP_KIND_F64: c_int = 3;
pub const ATP_KIND_BOOL: c_int = 4;
pub const ATP_KIND_TEXT: c_int = 5;
pub const ATP_KIND_BLOB: c_int = 6;

pub const ATP_STATE: c_int = 0;
pub const ATP_QUEUE: c_int = 1;

pub const ATP_DROP_OLDEST: c_int = 0;
pub const ATP_DROP_INCOMING: c_int = 1;

pub const ATP_LOG_ERROR: c_int = 0;
pub const ATP_LOG_WARNING: c_int = 1;
pub const ATP_LOG_INFO: c_int = 2;
pub const ATP_LOG_DEBUG: c_int = 3;

/// Success of a lifecycle call.
pub const ATP_OK: c_int = 0;

pub const ATP_WORK_BUSY: c_int = 0;
pub const ATP_WORK_IDLE: c_int = 1;
pub const ATP_WORK_ERROR: c_int = -1;

/// Handle naming no config node. The root's handle is never zero, so one comparison covers a failed
/// lookup, an index out of range and "there is nothing here" alike.
pub const ATP_CONFIG_NONE: u32 = 0;

pub const ATP_CONFIG_NULL: c_int = 0;
pub const ATP_CONFIG_BOOL: c_int = 1;
pub const ATP_CONFIG_INT: c_int = 2;
pub const ATP_CONFIG_REAL: c_int = 3;
pub const ATP_CONFIG_TEXT: c_int = 4;
pub const ATP_CONFIG_ARRAY: c_int = 5;
pub const ATP_CONFIG_OBJECT: c_int = 6;

/// The host's per-instance context: opaque, and only ever passed back where it came from.
#[repr(C)]
pub struct AtpCtx {
    _opaque: [u8; 0],
}

/// The byte view of a text or blob value.
///
/// Not a `&[u8]`, deliberately: coming from the host the pointer is only valid until the next reading
/// call, and a Rust slice would claim a lifetime the C contract does not give.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct AtpBytes {
    pub data: *const c_char,
    pub size: usize,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union AtpValuePayload {
    pub as_i32: i32,
    pub as_i64: i64,
    pub as_f64: f64,
    pub as_bool: c_int,
    pub as_bytes: AtpBytes,
}

/// One value crossing the boundary. `kind` is checked by the host against the port's declared kind,
/// so a wrong pairing is refused rather than reinterpreted.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct AtpValue {
    pub kind: c_int,
    pub payload: AtpValuePayload,
}

impl AtpValue {
    /// A value to hand to the host as an out-parameter. The kind is meaningless until the host has
    /// filled it in.
    pub fn empty() -> Self {
        Self { kind: 0, payload: AtpValuePayload { as_i64: 0 } }
    }

    pub fn i32(value: i32) -> Self {
        Self { kind: ATP_KIND_I32, payload: AtpValuePayload { as_i32: value } }
    }

    pub fn i64(value: i64) -> Self {
        Self { kind: ATP_KIND_I64, payload: AtpValuePayload { as_i64: value } }
    }

    pub fn f64(value: f64) -> Self {
        Self { kind: ATP_KIND_F64, payload: AtpValuePayload { as_f64: value } }
    }

    pub fn bool(value: bool) -> Self {
        Self { kind: ATP_KIND_BOOL, payload: AtpValuePayload { as_bool: c_int::from(value) } }
    }

    /// Borrows @p text for the duration of the call that receives this value — which is all the host
    /// asks for, since it copies whatever it keeps.
    pub fn text(text: &str) -> Self {
        Self {
            kind: ATP_KIND_TEXT,
            payload: AtpValuePayload {
                as_bytes: AtpBytes { data: text.as_ptr().cast::<c_char>(), size: text.len() },
            },
        }
    }

    /// The same for opaque bytes, which is the escape hatch from the closed set of payload types.
    pub fn blob(bytes: &[u8]) -> Self {
        Self {
            kind: ATP_KIND_BLOB,
            payload: AtpValuePayload {
                as_bytes: AtpBytes { data: bytes.as_ptr().cast::<c_char>(), size: bytes.len() },
            },
        }
    }
}

/// Everything the host offers one module instance. Ports are addressed by their index in the
/// descriptor's arrays.
///
/// Every callback is `noexcept` on the host's side and reports failure by return value, so no C++
/// exception can unwind a Rust frame. The other direction is this plugin's job — see the panic guard
/// in `lib.rs`.
#[repr(C)]
pub struct AtpApi {
    pub struct_size: u32,
    pub get_input: unsafe extern "C" fn(*mut AtpCtx, u32, *mut AtpValue) -> c_int,
    pub take_input: unsafe extern "C" fn(*mut AtpCtx, u32, *mut AtpValue) -> c_int,
    pub write_output: unsafe extern "C" fn(*mut AtpCtx, u32, *const AtpValue) -> c_int,
    pub get_property: unsafe extern "C" fn(*mut AtpCtx, u32, *mut AtpValue) -> c_int,
    pub take_property: unsafe extern "C" fn(*mut AtpCtx, u32, *mut AtpValue) -> c_int,
    pub set_property: unsafe extern "C" fn(*mut AtpCtx, u32, *const AtpValue) -> c_int,
    pub stop_requested: unsafe extern "C" fn(*mut AtpCtx) -> c_int,
    pub log: unsafe extern "C" fn(*mut AtpCtx, c_int, *const c_char, usize),
    pub wake: unsafe extern "C" fn(*mut AtpCtx),
    pub set_error: unsafe extern "C" fn(*mut AtpCtx, *const c_char, usize),
    pub config_root: unsafe extern "C" fn(*mut AtpCtx) -> u32,
    pub config_kind: unsafe extern "C" fn(*mut AtpCtx, u32) -> c_int,
    pub config_size: unsafe extern "C" fn(*mut AtpCtx, u32) -> u32,
    pub config_key_at: unsafe extern "C" fn(*mut AtpCtx, u32, u32, *mut *const c_char, *mut usize) -> c_int,
    pub config_child_at: unsafe extern "C" fn(*mut AtpCtx, u32, u32) -> u32,
    pub config_find: unsafe extern "C" fn(*mut AtpCtx, u32, *const c_char, usize) -> u32,
    pub config_value_of: unsafe extern "C" fn(*mut AtpCtx, u32, *mut AtpValue) -> c_int,
}

#[repr(C)]
pub struct AtpInputDesc {
    pub name: *const c_char,
    pub kind: c_int,
    pub flavor: c_int,
    /// `ATP_QUEUE` only; zero asks for the platform's own default limit, not for an unbounded queue.
    pub capacity: u32,
    pub overflow: c_int,
}

#[repr(C)]
pub struct AtpOutputDesc {
    pub name: *const c_char,
    pub kind: c_int,
}

#[repr(C)]
pub struct AtpPropertyDesc {
    pub name: *const c_char,
    pub kind: c_int,
    /// The default in the platform's canonical string form; parsed by the same code that parses a
    /// config value, so a malformed one fails the load.
    pub default_value: *const c_char,
    /// Allowed values, also as canonical strings — an enumeration. Null leaves it unconstrained.
    pub options: *const *const c_char,
    pub option_count: u32,
    pub persistent: c_int,
}

pub type AtpCreateFn = unsafe extern "C" fn(*const AtpApi, *mut AtpCtx, *mut c_void) -> *mut c_void;
pub type AtpDestroyFn = unsafe extern "C" fn(*mut c_void);
pub type AtpLifecycleFn = unsafe extern "C" fn(*mut c_void) -> c_int;
pub type AtpIterateFn = unsafe extern "C" fn(*mut c_void) -> c_int;

/// What the host needs to offer one module. `Option` on the function pointers is how the header's
/// nullable fields are spelled: `initialize`, `start` and `stop` may be absent, the rest may not.
#[repr(C)]
pub struct AtpModuleDesc {
    pub struct_size: u32,
    pub name: *const c_char,
    pub version: [u32; 4],
    pub version_count: u32,
    pub inputs: *const AtpInputDesc,
    pub input_count: u32,
    pub outputs: *const AtpOutputDesc,
    pub output_count: u32,
    pub properties: *const AtpPropertyDesc,
    pub property_count: u32,
    pub user_data: *mut c_void,
    pub create: Option<AtpCreateFn>,
    pub destroy: Option<AtpDestroyFn>,
    pub initialize: Option<AtpLifecycleFn>,
    pub start: Option<AtpLifecycleFn>,
    pub iterate: Option<AtpIterateFn>,
    pub stop: Option<AtpLifecycleFn>,
    /// Where the module is declared, or null when it is declared nowhere a person could open —
    /// which is the case for a compiled plugin like this one.
    pub source: *const c_char,
}

/// A NUL-terminated name for a descriptor field, out of a byte literal that already ends in one.
///
/// The host reads these as C strings and keeps the pointers until the library is unloaded, so they
/// have to be `'static` — which every byte literal is.
pub const fn cstr(text: &'static [u8]) -> *const c_char {
    text.as_ptr().cast::<c_char>()
}
