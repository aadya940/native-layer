![CI](https://github.com/aadya940/native-layer/actions/workflows/ci.yml/badge.svg)

# native-layer

Call native C/C++/Rust/Zig code directly from LLM agents. Drop a compiled plugin into a directory, and the agent discovers and calls it at runtime: no FFI, no binding generators, no Python glue.

```
pip install .
```



### Why
**The missing link between AI agents and the real world.**
Agent frameworks let LLMs call Python functions and APIs. But what if you need your agent to:
- Use OpenCV for computer vision
- Control a robot running ROS
- Access CUDA for GPU computation
- Integrate with 20 years of C++ production code
- Run completely offline on embedded hardware

Right now, you're stuck rewriting everything in Python or building one-off FFI wrappers.

**Native Layer solves this.** Drop a compiled plugin (.so/.dll) into a directory, and your 
agent can call it, no bindings to write, no API servers to deploy, no Python rewrites.

- **Any language**: C, C++, Rust, Zig, anything with C ABI
- **Any framework**: Works with LangChain, Google ADK, Ollama, etc.
- **Hot-reload**: Add new tools without restarting
- **Offline-first**: No network required, runs on-device

This opens agents to massive ecosystems: robotics (ROS), computer vision (OpenCV), scientific 
computing (BLAS/LAPACK), hardware drivers, legacy systems, decades of reliable code 
agents couldn't access before.

MCP excels at remote tools over the network. Native Layer is built for local execution: 
robots, drones, edge devices, and any system where the agent needs to touch hardware or 
run without internet.

## How it works

A plugin is a shared library that exports one function:

```c
PluginAPI* get_plugin_api();
```

`PluginAPI` is a struct with function pointers for schema discovery, execution, and cleanup. The host calls `get_plugin_api()` on load, reads the JSON schema to understand what the plugin does, and wires it into the agent's tool list automatically.

Hot-reload is built in. Drop a new `.dll`/`.so` into the watched directory and the agent picks it up without restarting.



## Writing a plugin

C++

```cpp
#include "native_layer/include/plugin_layer.h"
#include <string.h>
#include <stdlib.h>

static const char* get_schema() {
    return R"({
        "name": "fast_math",
        "description": "Multiply every element of a vector by 2.",
        "parameters": {
            "type": "object",
            "properties": {
                "vector": { "type": "array", "items": { "type": "number" } }
            },
            "required": ["vector"]
        }
    })";
}

static int execute(const char* fn, const MemoryBuffer* inputs, size_t n, MemoryBuffer* out) {
    if (strcmp(fn, "fast_math") != 0) return -1;

    double* data  = (double*)inputs[0].data;
    size_t  count = inputs[0].size / sizeof(double);
    double* result = (double*)malloc(count * sizeof(double));

    for (size_t i = 0; i < count; i++)
        result[i] = data[i] * 2.0;

    out->data    = result;
    out->size    = count * sizeof(double);
    out->type_id = TYPE_BUFFER;
    out->dtype   = DTYPE_F64;
    return 0;
}

static void free_buffer(MemoryBuffer* b) { free(b->data); }
static int  init(const char* cfg)        { return 0; }
static void shut()                       {}

static PluginAPI api = {
    "math_lib", "1.0",
    get_schema, init, shut, execute, free_buffer
};

extern "C" EXPORT PluginAPI* get_plugin_api() { return &api; }
```


Rust
```rust
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
            (*output).shape = std::ptr::null_mut();
            (*output).ndim = 0;
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
```

Compile to a shared library, drop it in your plugins directory. That's the entire integration.



## Using with an agent framework

#### Google ADK

```python
from google.adk.agents import Agent
from google.adk.runners import Runner
from google.adk.sessions import InMemorySessionService
from native_layer import NativeManager
from native_layer.adapters.adk import NativeADKToolset

manager = NativeManager()
manager.watch_directory("./plugins")

native_tools = NativeADKToolset(manager)

agent = Agent(
    model="gemini-2.0-flash",
    name="my_agent",
    tools=[native_tools],
)

session_service = InMemorySessionService()
runner = Runner(agent=agent, app_name="demo", session_service=session_service)
```

#### LangChain

```python
from native_layer import NativeManager
from native_layer.adapters.langchain import NativeHotReloadMiddleware
from langchain.agents import AgentExecutor, create_openai_tools_agent

manager = NativeManager()
manager.watch_directory("./plugins")

middleware = NativeHotReloadMiddleware(manager)

agent = create_openai_tools_agent(llm, tools=[], prompt=prompt)
executor = AgentExecutor(agent=agent, tools=[]).with_middleware(middleware)
```


## Supported frameworks

| Framework | Adapter | Status |
|---|---|---|
| Google ADK ≥ 1.25.0 | `NativeADKToolset` | ✅ |
| LangChain ≥ 1.0 | `NativeHotReloadMiddleware` | ✅ |
| Ollama Tool Use | `get_ollama_tools` | ✅ |

## Type system

The host marshals Python values into `MemoryBuffer` structs before calling your plugin. Buffer element dtype is detected automatically from the Python buffer's format string.

| C type | `type_id` | Python side |
|---|---|---|
| `double[]` / `float[]` / `int64[]` | `TYPE_BUFFER` | `memoryview`, `array.array`, NumPy array |
| `double` scalar | `TYPE_FLOAT` | `float` |
| `int64_t` scalar | `TYPE_INT` | `int` |
| UTF-8 string | `TYPE_STRING` | `str` |
| buffer type | `TYPE_BUFFER` + `DTYPE_BYTES` + `shape` + `ndim` | `bytes` |
| json type | `TYPE_JSON` | `json` |
| type opaque | `TYPE_OPAQUE` | (void*) data |

## Platform support

| Platform | Status | Notes |
|---|---|---|
| Windows x64 | ✅ Tested | MSVC, `.dll` |
| Linux x64 | ✅ Should work | GCC/Clang, `.so` |
| macOS | ✅ Should wor | Clang, `.dylib`, expected to work |

## Security

Plugins run in-process and have full system access.

The runtime includes **crash protection** so that plugin segmentation faults do not terminate the agent process. However, plugins are not sandboxed and may still perform arbitrary operations.

Only load plugins you trust.

Future versions will explore:
- plugin code signing
- sandboxed execution
- permission models



## Roadmap

- [ ] Zig plugin example
- [ ] Multi-Agent and Multi-Language Tools Example
- [ ] Security 


## Contributing

Issues and PRs welcome. If you build a plugin for something useful, open a PR to add it to the examples.



## License

Apache 2.0
