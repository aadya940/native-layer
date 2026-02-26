# native-layer

Lets LLM agents call native C/C++/Rust/Zig code as tools. You write a plain C export, the library wraps it into a typed LangChain or ADK tool automatically. No FFI, no binding framework, no Python glue code.

This opens the full native ecosystem to agentic tools, taking them as close to the operating system as native code allows.

> **Security caveat:** plugins run in-process with full system access. There's no sandboxing or code signing. Only load plugins you trust.

## Writing a plugin

Five functions, one struct, compile to a DLL.

```cpp
#include "native_layer/include/plugin_layer.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT
#endif

// 1. JSON Schema (Raw String Literal)
static const char* get_schema() {
    return R"({
        "name": "fast_math",
        "description": "High-performance vector operations.",
        "parameters": {
            "type": "object",
            "properties": {
                "vector": { "type": "array", "items": { "type": "number" } }
            },
            "required": ["vector"]
        }
    })";
}

// 2. Execution Logic
static int execute(const char* fn, const MemoryBuffer* inputs, size_t n, MemoryBuffer* out) {
    if (strcmp(fn, "fast_math") != 0) return -1;

    // Zero-copy access to Python memory
    double* data = (double*)inputs[0].data;
    size_t count = inputs[0].size / sizeof(double);

    // Allocate result (Host calls free_buffer later)
    double* result = (double*)malloc(count * sizeof(double));
    
    for(size_t i=0; i<count; i++) {
        result[i] = data[i] * 2.0; // Example computation
    }

    out->data = result;
    out->size = count * sizeof(double);
    out->type_id = TYPE_BUFFER;
    out->dtype = DTYPE_F64;
    return 0;
}

// 3. Cleanup
static void free_buffer(MemoryBuffer* b) { free(b->data); }

// 4. Lifecycle (Optional)
static int init(const char* cfg) { return 0; }
static void shut() {}

// 5. Export Table
static PluginAPI api = { "math_lib", "1.0", get_schema, init, shut, execute, free_buffer };
extern "C" EXPORT PluginAPI* get_plugin_api() { return &api; }
```

The host allocates inputs and passes them zero-copy to your plugin. Your plugin allocates the output; `free_buffer` is called by the host when it's done reading.

## How to use

#### LangChain

```python
from native_layer import NativeManager
from native_layer.adapters.langchain import NativeHotReloadMiddleware
from langchain.agents import AgentExecutor, create_openai_tools_agent

# Initialize Manager & Watcher
manager = NativeManager()
manager.watch_directory("./plugins")

# Attach Middleware
middleware = NativeHotReloadMiddleware(manager)

# Create Agent: Tools list is empty, Middleware will handle the injection
agent = create_openai_tools_agent(llm, tools=[], prompt=prompt)
executor = AgentExecutor(agent=agent, tools=[]).with_middleware(middleware)

# Invoke
executor.invoke({"input": "Run fast_math on this array..."})
```

#### Google ADK

```python
from google.adk import Agent
from native_layer import NativeManager
from native_layer.adapters.adk import NativeADKToolset

manager = NativeManager()
manager.watch_directory("./plugins")

agent = Agent(
    model=model,
    tools=[NativeADKToolset(manager)] # Tools are queried JIT
)

agent.run("Use the physics plugin to calculate trajectory.")
```

## Supported frameworks

| Framework | Adapter |
|---|---|
| [LangChain 1.x](https://blog.langchain.com/agent-middleware/) | `NativeHotReloadMiddleware` |
| [Google ADK](https://google.github.io/adk-docs/tools-custom/#the-basetoolset-interface) | `NativeADKToolset` |

## Type support

| C type | `type_id` | Python side |
|---|---|---|
| `double[]` / `float[]` / `int64[]` | `TYPE_BUFFER` | `memoryview`, `array.array`, NumPy array |
| `double` scalar | `TYPE_FLOAT` | `float` |
| `int64_t` scalar | `TYPE_INT` | `int` |
| UTF-8 string | `TYPE_STRING` | `str` |
| opaque bytes | `TYPE_BUFFER` + `DTYPE_BYTES` | `bytes` |

Buffer element dtype (`DTYPE_*`) is detected automatically from the Python buffer's format string : `'d'` → F64, `'f'` → F32, `'q'` → I64.

## Platform support

| Platform | Status | Notes |
|---|---|---|
| Windows (x64) | ✅ Tested | MSVC, `.dll` |
| Linux (x64) | ✅ Should work | GCC/Clang, `.so` |
| macOS | ⚠️ Untested | Clang, `.dylib`, expected to work |
| Windows DLL hot-reload | ⚠️ Limited | Windows locks loaded DLLs: copy to a shadow path before replacing |

## Building

Requires Visual Studio 2019+ (MSVC), Python 3.10+, pybind11.

**1. Build the Python extension** (x64 Native Tools Command Prompt):

```
cmake -S native_layer -B native_layer/build
msbuild native_layer\build\AgentBridge.sln /p:Configuration=Release
```

**2. Build the test plugin:**

```
cl /LD /EHsc /std:c++17 native_layer\tests\plugins\libmath.cpp ^
   /I native_layer\include ^
   /Fe:native_layer\tests\plugins\libmath.dll
```

**3. Install Python dependencies:**

```
pip install langchain>=1.0 google-adk>=1.25.0 pybind11 pytest
```

**4. Run the tests:**

```
pytest
```
