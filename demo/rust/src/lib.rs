#![allow(non_camel_case_types)]

#[path = "../../../native_layer/include/bindings/plugin_layer.rs"]
mod plugin_layer;

use plugin_layer::{
    PluginAPI, MemoryBuffer,
    TYPE_STRING, TYPE_UNKNOWN,
    DTYPE_BYTES,
    GetSchemaFn, InitializeFn, ShutdownFn,
    ExecuteFn, FreeBufferFn,
};

use libc::{c_char, c_void};
use serde_json::Value;
use std::ffi::CStr;
use std::ptr;

static NAME: &[u8] = b"plugin_rust\0";
static VERSION: &[u8] = b"1.0\0";

static SCHEMA: &[u8] = b"[
  {
    \"name\": \"json_pretty\",
    \"description\": \"Pretty-print a JSON string using serde_json\",
    \"parameters\": {
      \"type\": \"object\",
      \"properties\": {
        \"json\": { \"type\": \"string\" }
      },
      \"required\": [\"json\"]
    }
  },
  {
    \"name\": \"json_get\",
    \"description\": \"Extract a value from JSON using a dot path\",
    \"parameters\": {
      \"type\": \"object\",
      \"properties\": {
        \"json\": { \"type\": \"string\" },
        \"path\": { \"type\": \"string\" }
      },
      \"required\": [\"json\", \"path\"]
    }
  }
]\0";


extern "C" fn get_schema() -> *const c_char {
    SCHEMA.as_ptr() as *const c_char
}

extern "C" fn initialize(_config: *const c_char) -> i32 {
    0
}

extern "C" fn shutdown() {}

extern "C" fn execute(
    function_name: *const c_char,
    inputs: *const MemoryBuffer,
    num_inputs: usize,
    output: *mut MemoryBuffer,
) -> i32 {

    if function_name.is_null() || inputs.is_null() || output.is_null() {
        return -1;
    }

    let fname = unsafe { CStr::from_ptr(function_name) };
    let inputs = unsafe { std::slice::from_raw_parts(inputs, num_inputs) };

    if fname.to_bytes() == b"json_pretty" {

        if num_inputs < 1 {
            return -1;
        }

        let input = &inputs[0];

        if input.type_id != TYPE_STRING || input.data.is_null() {
            return -1;
        }

        let bytes = unsafe {
            std::slice::from_raw_parts(input.data as *const u8, input.size)
        };

        let json_str = match std::str::from_utf8(bytes) {
            Ok(s) => s,
            Err(_) => return -1,
        };

        let value: Value = match serde_json::from_str(json_str) {
            Ok(v) => v,
            Err(_) => return -1,
        };

        let pretty = match serde_json::to_string_pretty(&value) {
            Ok(s) => s,
            Err(_) => return -1,
        };

        let out = pretty.as_bytes();

        let mem = unsafe { libc::malloc(out.len().max(1)) };
        if mem.is_null() {
            return -1;
        }

        unsafe {
            ptr::copy_nonoverlapping(out.as_ptr(), mem as *mut u8, out.len());

            (*output).data = mem;
            (*output).size = out.len();
            (*output).type_id = TYPE_STRING;
            (*output).dtype = DTYPE_BYTES;
        }

        return 0;
    }

    if fname.to_bytes() == b"json_get" {

        if num_inputs < 2 {
            return -1;
        }

        let json_buf = &inputs[0];
        let path_buf = &inputs[1];

        if json_buf.type_id != TYPE_STRING || path_buf.type_id != TYPE_STRING {
            return -1;
        }

        let json_bytes = unsafe {
            std::slice::from_raw_parts(json_buf.data as *const u8, json_buf.size)
        };

        let path_bytes = unsafe {
            std::slice::from_raw_parts(path_buf.data as *const u8, path_buf.size)
        };

        let json_str = match std::str::from_utf8(json_bytes) {
            Ok(s) => s,
            Err(_) => return -1,
        };

        let path = match std::str::from_utf8(path_bytes) {
            Ok(s) => s,
            Err(_) => return -1,
        };

        let value: Value = match serde_json::from_str(json_str) {
            Ok(v) => v,
            Err(_) => return -1,
        };

        let mut current = &value;

        for part in path.split('.') {
            current = match current.get(part) {
                Some(v) => v,
                None => return -1,
            };
        }

        let result = match serde_json::to_string(current) {
            Ok(s) => s,
            Err(_) => return -1,
        };

        let out = result.as_bytes();

        let mem = unsafe { libc::malloc(out.len().max(1)) };
        if mem.is_null() {
            return -1;
        }

        unsafe {
            ptr::copy_nonoverlapping(out.as_ptr(), mem as *mut u8, out.len());

            (*output).data = mem;
            (*output).size = out.len();
            (*output).type_id = TYPE_STRING;
            (*output).dtype = DTYPE_BYTES;
        }

        return 0;
    }

    -1
}

extern "C" fn free_buffer(buffer: *mut MemoryBuffer) {

    if buffer.is_null() {
        return;
    }

    unsafe {

        if !(*buffer).data.is_null() {
            libc::free((*buffer).data);
        }

        (*buffer).data = ptr::null_mut::<c_void>();
        (*buffer).size = 0;
        (*buffer).type_id = TYPE_UNKNOWN;
        (*buffer).dtype = DTYPE_BYTES;
    }
}


static API: PluginAPI = PluginAPI {

    name: NAME.as_ptr() as *const c_char,
    version: VERSION.as_ptr() as *const c_char,

    get_schema: Some(get_schema as GetSchemaFn),
    initialize: Some(initialize as InitializeFn),
    shutdown: Some(shutdown as ShutdownFn),

    execute: Some(execute as ExecuteFn),
    free_buffer: Some(free_buffer as FreeBufferFn),
};


#[no_mangle]
pub extern "C" fn get_plugin_api() -> *mut PluginAPI {
    &API as *const PluginAPI as *mut PluginAPI
}
