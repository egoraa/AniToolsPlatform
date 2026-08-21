// SPDX-License-Identifier: Apache-2.0

//! A platform module written in Rust, loaded through the C path of the plugin ABI.
//!
//! Nothing here links the SDK, includes a C++ header or runs a C++ compiler: the whole contract is
//! `abi.rs`, a mirror of `atp/plugin_c.h`, and three exported C functions. The host builds real
//! platform ports from the descriptor below, so this module connects to a C++ one with nothing in
//! between — `pipeline.json` puts it between two modules of `atp_demo_plugin`.
//!
//! The module keeps a sliding window of the values it receives and reports their mean. It is small on
//! purpose, but it is not a toy in one respect: the `panic` property makes it panic, and what happens
//! then is the thing worth copying out of this file. A panic unwinding into a C++ frame is undefined
//! behaviour, so every entry point is wrapped in `catch_unwind` and reports the panic as an ordinary
//! module error. That is better than `panic = "abort"` in the profile, which would take the host down
//! with it and lose the diagnosis.

mod abi;

use abi::*;
use core::ffi::{c_char, c_int, c_void};
use std::any::Any;
use std::collections::VecDeque;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::OnceLock;

const IN_VALUE: u32 = 0;

const OUT_REPORT: u32 = 0;
const OUT_MEAN: u32 = 1;

const PROP_WINDOW: u32 = 0;
const PROP_MODE: u32 = 1;
const PROP_PANIC: u32 = 2;

/// State of one instance: what a C++ module would keep in its members.
struct Averager {
    api: &'static AtpApi,
    ctx: *mut AtpCtx,
    recent: VecDeque<i32>,
    weights: Vec<f64>,
}

/// Reads the `weights` list out of the module's config, which is legal here and nowhere else: the
/// config is readable from `create`, the C path's analogue of a constructor, and this is the channel
/// for a setting that has to be known before a port is connected.
///
/// A list of numbers is what a property cannot be, and it is not edited while the pipeline runs —
/// `window` stays a property for exactly the opposite reasons. Everything here degrades to an empty
/// vector: the host may predate the accessors, the node may have named no config, and a probe
/// instance built to discover this module's ports is handed an empty one.
fn read_weights(api: &AtpApi, ctx: *mut AtpCtx) -> Vec<f64> {
    if !api.has_config() {
        return Vec::new();
    }
    let key = b"weights";
    let root = unsafe { (api.config_root)(ctx) };
    let node = unsafe { (api.config_find)(ctx, root, key.as_ptr().cast::<c_char>(), key.len()) };
    if node == ATP_CONFIG_NONE || unsafe { (api.config_kind)(ctx, node) } != ATP_CONFIG_ARRAY {
        return Vec::new();
    }
    let count = unsafe { (api.config_size)(ctx, node) };
    let mut weights = Vec::with_capacity(count as usize);
    for i in 0..count {
        let child = unsafe { (api.config_child_at)(ctx, node, i) };
        let mut value = AtpValue::empty();
        if unsafe { (api.config_value_of)(ctx, child, &mut value) } != 1 {
            continue;
        }
        match value.kind {
            ATP_KIND_I64 => weights.push(unsafe { value.payload.as_i64 } as f64),
            ATP_KIND_F64 => weights.push(unsafe { value.payload.as_f64 }),
            _ => {}
        }
    }
    weights
}

impl Averager {
    fn log(&self, level: c_int, text: &str) {
        unsafe { (self.api.log)(self.ctx, level, text.as_ptr().cast::<c_char>(), text.len()) }
    }

    fn fail(&self, text: &str) {
        unsafe { (self.api.set_error)(self.ctx, text.as_ptr().cast::<c_char>(), text.len()) }
    }

    fn stopping(&self) -> bool {
        unsafe { (self.api.stop_requested)(self.ctx) == 1 }
    }

    fn take_i32(&self, port: u32) -> Option<i32> {
        let mut value = AtpValue::empty();
        if unsafe { (self.api.take_input)(self.ctx, port, &mut value) } == 1 {
            Some(unsafe { value.payload.as_i32 })
        } else {
            None
        }
    }

    fn write(&self, port: u32, value: &AtpValue) -> bool {
        unsafe { (self.api.write_output)(self.ctx, port, value) == 1 }
    }

    fn property_i32(&self, index: u32) -> i32 {
        let mut value = AtpValue::empty();
        if unsafe { (self.api.get_property)(self.ctx, index, &mut value) } == 1 {
            unsafe { value.payload.as_i32 }
        } else {
            0
        }
    }

    fn property_bool(&self, index: u32) -> bool {
        let mut value = AtpValue::empty();
        if unsafe { (self.api.get_property)(self.ctx, index, &mut value) } == 1 {
            unsafe { value.payload.as_bool != 0 }
        } else {
            false
        }
    }

    /// Reads a text property, copying it out.
    ///
    /// The copy is the contract, not caution: the bytes the host hands over stay valid only until the
    /// next reading call, and this function's caller goes on to make several.
    fn property_text(&self, index: u32) -> String {
        let mut value = AtpValue::empty();
        if unsafe { (self.api.get_property)(self.ctx, index, &mut value) } != 1 {
            return String::new();
        }
        let bytes = unsafe { value.payload.as_bytes };
        if bytes.data.is_null() {
            return String::new();
        }
        let slice = unsafe { core::slice::from_raw_parts(bytes.data.cast::<u8>(), bytes.size) };
        String::from_utf8_lossy(slice).into_owned()
    }

    /// Weighted over the tail when the config named weights, a plain mean otherwise.
    fn mean(&self) -> f64 {
        if self.weights.is_empty() {
            let sum: f64 = self.recent.iter().copied().map(f64::from).sum();
            return sum / self.recent.len() as f64;
        }
        let taken = self.recent.len().min(self.weights.len());
        let values = self.recent.iter().rev().take(taken);
        let weights = self.weights.iter().rev().take(taken);
        let total: f64 = weights.clone().sum();
        let sum: f64 = values.zip(weights).map(|(v, w)| f64::from(*v) * w).sum();
        if total == 0.0 {
            0.0
        } else {
            sum / total
        }
    }

    fn pass(&mut self) -> c_int {
        if self.property_bool(PROP_PANIC) {
            panic!("the panic property was set");
        }
        let verbose = self.property_text(PROP_MODE) == "verbose";
        let window = self.property_i32(PROP_WINDOW).max(1) as usize;
        let mut status = ATP_WORK_IDLE;

        while let Some(value) = self.take_i32(IN_VALUE) {
            self.recent.push_back(value);
            while self.recent.len() > window {
                self.recent.pop_front();
            }
            let mean = self.mean();

            let report = if verbose {
                format!("rust_averager: {value} -> mean {mean:.3} over {} of {window}", self.recent.len())
            } else {
                format!("rust_averager: mean {mean:.3}")
            };
            if !self.write(OUT_REPORT, &AtpValue::text(&report)) || !self.write(OUT_MEAN, &AtpValue::f64(mean)) {
                self.fail("an output refused the value");
                return ATP_WORK_ERROR;
            }
            status = ATP_WORK_BUSY;

            if self.stopping() {
                break;
            }
        }
        status
    }
}

/// Turns a caught panic into a message worth putting in a log.
fn panic_text(payload: &(dyn Any + Send)) -> String {
    if let Some(text) = payload.downcast_ref::<&str>() {
        return format!("panicked: {text}");
    }
    if let Some(text) = payload.downcast_ref::<String>() {
        return format!("panicked: {text}");
    }
    String::from("panicked")
}

/// Runs a lifecycle step under the panic guard, reporting a panic the way any other failure is
/// reported. `self_` is what `create` returned, so it is never null.
unsafe fn guarded<F: FnOnce(&mut Averager) -> c_int>(self_: *mut c_void, body: F, failed: c_int) -> c_int {
    let state = &mut *(self_.cast::<Averager>());
    let api = state.api;
    let ctx = state.ctx;
    match catch_unwind(AssertUnwindSafe(|| body(state))) {
        Ok(status) => status,
        Err(payload) => {
            let text = panic_text(payload.as_ref());
            (api.set_error)(ctx, text.as_ptr().cast::<c_char>(), text.len());
            failed
        }
    }
}

unsafe extern "C" fn create(api: *const AtpApi, ctx: *mut AtpCtx, _user_data: *mut c_void) -> *mut c_void {
    let made = catch_unwind(AssertUnwindSafe(|| {
        let api = &*api;
        let weights = read_weights(api, ctx);
        Box::into_raw(Box::new(Averager { api, ctx, recent: VecDeque::new(), weights })).cast::<c_void>()
    }));
    made.unwrap_or(core::ptr::null_mut())
}

unsafe extern "C" fn destroy(self_: *mut c_void) {
    if self_.is_null() {
        return;
    }
    // A drop that unwinds would cross the boundary, and there is nothing left to report it to.
    let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(self_.cast::<Averager>()))));
}

unsafe extern "C" fn start(self_: *mut c_void) -> c_int {
    guarded(
        self_,
        |state| {
            let window = state.property_i32(PROP_WINDOW);
            state.log(ATP_LOG_INFO, &format!("averaging over a window of {window}"));
            ATP_OK
        },
        1,
    )
}

unsafe extern "C" fn stop(self_: *mut c_void) -> c_int {
    guarded(
        self_,
        |state| {
            state.recent.clear();
            ATP_OK
        },
        1,
    )
}

unsafe extern "C" fn iterate(self_: *mut c_void) -> c_int {
    guarded(self_, Averager::pass, ATP_WORK_ERROR)
}

/// The descriptor, and everything it points at.
///
/// Built once on demand rather than as a `static`, because a `static` full of raw pointers into other
/// statics is a const-evaluation puzzle for no gain. The tables are leaked on purpose: the host keeps
/// these pointers until the library is unloaded, which is exactly the lifetime a leak gives them, and
/// there is nothing left to free by then. Taking `as_ptr()` before the boxes are leaked is sound —
/// what moves is the handle, never the heap buffer it points at.
struct Registry {
    module: AtpModuleDesc,
}

// The raw pointers inside are read by the host, never written, and outlive every reader.
unsafe impl Sync for Registry {}
unsafe impl Send for Registry {}

static REGISTRY: OnceLock<Registry> = OnceLock::new();

fn registry() -> &'static Registry {
    REGISTRY.get_or_init(|| {
        let inputs: &'static [AtpInputDesc] = Box::leak(Box::new([AtpInputDesc {
            name: cstr(b"value\0"),
            kind: ATP_KIND_I32,
            // A queue rather than a state input: a value arriving between two passes must not be
            // dropped, or the mean would depend on scheduling.
            flavor: ATP_QUEUE,
            capacity: 64,
            overflow: ATP_DROP_OLDEST,
        }]));

        let outputs: &'static [AtpOutputDesc] = Box::leak(Box::new([
            AtpOutputDesc { name: cstr(b"report\0"), kind: ATP_KIND_TEXT },
            AtpOutputDesc { name: cstr(b"mean\0"), kind: ATP_KIND_F64 },
        ]));

        let windows: &'static [*const c_char] =
            Box::leak(Box::new([cstr(b"2\0"), cstr(b"4\0"), cstr(b"8\0")]));
        let modes: &'static [*const c_char] = Box::leak(Box::new([cstr(b"plain\0"), cstr(b"verbose\0")]));

        let properties: &'static [AtpPropertyDesc] = Box::leak(Box::new([
            AtpPropertyDesc {
                name: cstr(b"window\0"),
                kind: ATP_KIND_I32,
                default_value: cstr(b"4\0"),
                options: windows.as_ptr(),
                option_count: windows.len() as u32,
                persistent: 1,
            },
            AtpPropertyDesc {
                name: cstr(b"mode\0"),
                kind: ATP_KIND_TEXT,
                default_value: cstr(b"plain\0"),
                options: modes.as_ptr(),
                option_count: modes.len() as u32,
                persistent: 1,
            },
            AtpPropertyDesc {
                name: cstr(b"panic\0"),
                kind: ATP_KIND_BOOL,
                default_value: cstr(b"false\0"),
                options: core::ptr::null(),
                option_count: 0,
                // Transient: a switch for showing what a panic does has no business in a saved
                // project.
                persistent: 0,
            },
        ]));

        Registry {
            module: AtpModuleDesc {
                struct_size: core::mem::size_of::<AtpModuleDesc>() as u32,
                name: cstr(b"rust_averager\0"),
                version: [1, 0, 0, 0],
                version_count: 2,
                inputs: inputs.as_ptr(),
                input_count: inputs.len() as u32,
                outputs: outputs.as_ptr(),
                output_count: outputs.len() as u32,
                properties: properties.as_ptr(),
                property_count: properties.len() as u32,
                user_data: core::ptr::null_mut(),
                create: Some(create),
                destroy: Some(destroy),
                initialize: None,
                start: Some(start),
                iterate: Some(iterate),
                stop: Some(stop),
                source: core::ptr::null(),
            },
        }
    })
}

#[no_mangle]
pub extern "C" fn atp_c_abi_version() -> u32 {
    ATP_C_ABI
}

#[no_mangle]
pub extern "C" fn atp_module_count() -> u32 {
    1
}

#[no_mangle]
pub extern "C" fn atp_module_desc_at(index: u32) -> *const AtpModuleDesc {
    if index == 0 {
        &registry().module
    } else {
        core::ptr::null()
    }
}
