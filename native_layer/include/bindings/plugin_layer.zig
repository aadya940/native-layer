const std = @import("std");

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

pub const MemoryBuffer = extern struct {
    data: ?*anyopaque,
    size: usize,
    type_id: u32,
    dtype: u32,
};

pub const GetSchemaFn = *const fn () callconv(.C) [*:0]const u8;
pub const InitializeFn = *const fn (config: [*:0]const u8) callconv(.C) c_int;
pub const ShutdownFn = *const fn () callconv(.C) void;
pub const ExecuteFn = *const fn (
    function_name: [*:0]const u8,
    inputs: [*]const MemoryBuffer,
    num_inputs: usize,
    output: *MemoryBuffer,
) callconv(.C) c_int;
pub const FreeBufferFn = *const fn (buffer: *MemoryBuffer) callconv(.C) void;

pub const PluginAPI = extern struct {
    name: [*:0]const u8,
    version: [*:0]const u8,

    get_schema: ?GetSchemaFn,
    initialize: ?InitializeFn,
    shutdown: ?ShutdownFn,

    execute: ?ExecuteFn,
    free_buffer: ?FreeBufferFn,
};

pub const GetPluginAPIFunc = *const fn () callconv(.C) *PluginAPI;
