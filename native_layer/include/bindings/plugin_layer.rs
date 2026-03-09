#![allow(non_camel_case_types)]

use core::ffi::c_void;
use core::ffi::c_char;

pub const TYPE_UNKNOWN: u32 = 0;
pub const TYPE_BUFFER: u32 = 1;
pub const TYPE_STRING: u32 = 2;
pub const TYPE_INT: u32 = 3;
pub const TYPE_FLOAT: u32 = 4;

pub const DTYPE_F64: u32 = 0;
pub const DTYPE_F32: u32 = 1;
pub const DTYPE_I64: u32 = 2;
pub const DTYPE_I32: u32 = 3;
pub const DTYPE_BYTES: u32 = 4;

#[repr(C)]
pub struct MemoryBuffer {
    pub data: *mut c_void,
    pub size: usize,
    pub type_id: u32,
    pub dtype: u32,
}

pub type GetSchemaFn = unsafe extern "C" fn() -> *const c_char;
pub type InitializeFn = unsafe extern "C" fn(config: *const c_char) -> i32;
pub type ShutdownFn = unsafe extern "C" fn();
pub type ExecuteFn = unsafe extern "C" fn(
    function_name: *const c_char,
    inputs: *const MemoryBuffer,
    num_inputs: usize,
    output: *mut MemoryBuffer,
) -> i32;
pub type FreeBufferFn = unsafe extern "C" fn(buffer: *mut MemoryBuffer);

#[repr(C)]
pub struct PluginAPI {
    pub name: *const c_char,
    pub version: *const c_char,

    pub get_schema: Option<GetSchemaFn>,
    pub initialize: Option<InitializeFn>,
    pub shutdown: Option<ShutdownFn>,

    pub execute: Option<ExecuteFn>,
    pub free_buffer: Option<FreeBufferFn>,
}

unsafe impl Sync for PluginAPI {}

pub type GetPluginAPIFunc = unsafe extern "C" fn() -> *mut PluginAPI;
