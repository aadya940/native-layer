![CI](https://github.com/aadya940/native-layer/actions/workflows/ci.yml/badge.svg)

# native-layer

Call native C/C++/Rust/Zig code directly from LLM agents. Drop a compiled plugin into a directory, and the agent discovers and calls it at runtime: no FFI, no binding generators, no Python glue.

```
pip install .
```



### Why
Most agent frameworks assume tools are Python functions or HTTP endpoints. If you want an agent to use performance-critical code, hardware interfaces, or existing C/C++/Rust/Zig libraries, you end up either rewriting them in Python or maintaining a pile of one-off FFI glue and serialization.

Native Layer standardizes that interface. Tools are compiled plugins that expose a stable C ABI (Rust/Zig/C/C++ all work). The agent can call native code in-process, avoiding network hops, and using zero-copy buffers when possible (e.g., numeric arrays / tensors), with safe fallbacks for complex types.

Long term, agents will move closer to the hardware robotics stacks, SIMD-heavy inference, OS-adjacent tooling where latency, memory copies, and system integration matter. Native Layer borrows from OS device-driver design: self-describing plugins + function pointer tables as the core contract.

MCP is excellent when tools are remote or need to be language-agnostic over the network. Native Layer is optimized for local, hardware-adjacent, low-latency agent tooling where network calls and repeated serialization are the bottleneck.

Also, hot reloading gives the ability to an AI Agent to write and register its own tools at runtime.

## How it works

A plugin is a shared library that exports one function:

```c
PluginAPI* get_plugin_api();
```

`PluginAPI` is a struct with function pointers for schema discovery, execution, and cleanup. The host calls `get_plugin_api()` on load, reads the JSON schema to understand what the plugin does, and wires it into the agent's tool list automatically.

Hot-reload is built in. Drop a new `.dll`/`.so` into the watched directory and the agent picks it up without restarting.



## Writing a plugin

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
| Ollama Tool Use | — | Planned |

## Type system

The host marshals Python values into `MemoryBuffer` structs before calling your plugin. Buffer element dtype is detected automatically from the Python buffer's format string.

| C type | `type_id` | Python side |
|---|---|---|
| `double[]` / `float[]` / `int64[]` | `TYPE_BUFFER` | `memoryview`, `array.array`, NumPy array |
| `double` scalar | `TYPE_FLOAT` | `float` |
| `int64_t` scalar | `TYPE_INT` | `int` |
| UTF-8 string | `TYPE_STRING` | `str` |
| Opaque bytes | `TYPE_BUFFER` + `DTYPE_BYTES` | `bytes` |

`'d'` → F64, `'f'` → F32, `'q'` → I64.

## Platform support

| Platform | Status | Notes |
|---|---|---|
| Windows x64 | ✅ Tested | MSVC, `.dll` |
| Linux x64 | ✅ Should work | GCC/Clang, `.so` |
| macOS | ⚠️ Untested | Clang, `.dylib`, expected to work |

**Windows hot-reload caveat:** Windows locks loaded DLLs. The host copies the plugin to a shadow path before loading so replacement works, but there is a brief window where the old version is still live.


## Security

Plugins run in-process with full system access. There is no sandboxing, no code signing, and no permission model. Only load plugins you compiled yourself or fully trust. This is the same tradeoff as any shared library loaded at runtime.

Sandboxing is on the roadmap.



## Roadmap

- [ ] macOS testing and CI
- [ ] Rust plugin example and SDK
- [ ] Zig plugin example
- [ ] OpenAI and Anthropic adapters
- [ ] Security 


## Contributing

Issues and PRs welcome. If you build a plugin for something useful, open a PR to add it to the examples.



## License

Apache 2.0
