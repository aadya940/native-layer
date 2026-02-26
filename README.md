# native-layer

Lets LLM agents call native C/C++/Rust/Zig code as tools. You write a plain C export, the library wraps it into a typed LangChain or ADK tool automatically. No FFI, no binding framework, no Python glue code.

This opens the full native ecosystem to agentic tools, taking them as close to the operating system as native code allows.

> **Security caveat:** plugins run in-process with full system access. There's no sandboxing or code signing. Only load plugins you trust.

## Writing a plugin

Five functions, one struct, compile to a DLL.

```c
#include "native_layer/include/plugin_layer.h"

static const char* get_schema() {
    return "{\"name\":\"fft\",\"description\":\"Fast Fourier Transform\","
           "\"parameters\":{\"properties\":{"
           "\"samples\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
           "\"sample_rate\":{\"type\":\"number\"}}}}";
}

static int initialize(const char* config) { return 0; }
static void shutdown() {}

static int execute(const char* fn, const MemoryBuffer* inputs, size_t n, MemoryBuffer* out) {
    if (strcmp(fn, "fft") != 0) return -1;

    double* samples     = (double*)inputs[0].data;
    size_t  num_samples = inputs[0].size / sizeof(double);
    double  sample_rate = *(double*)inputs[1].data;

    // ... your native computation ...

    out->data    = result;
    out->size    = num_samples * sizeof(double);
    out->type_id = TYPE_BUFFER;
    return 0;
}

static void free_buffer(MemoryBuffer* b) { free(b->data); b->data = NULL; }

static PluginAPI api = { "signal", "1.0", get_schema, initialize, shutdown, execute, free_buffer };
EXPORT PluginAPI* get_plugin_api() { return &api; }
```

The host allocates inputs and passes them zero-copy to your plugin. Your plugin allocates the output; `free_buffer` is called by the host when it's done reading.

## How to use

**Initial setup:**

```python
from native_layer import NativeManager
from native_layer.adapters.langchain import NativeHotReloadMiddleware
from langchain.agents import create_agent

manager = NativeManager()
manager.load_plugin("signal", "./libsignal.dll")

middleware = NativeHotReloadMiddleware(manager)
agent = create_agent(your_llm, middleware._get_live_tools())
agent.invoke({"messages": [HumanMessage(content="run fft on this data")]})
```

**Hot reload** — drop a new DLL into a watched directory while the agent runs:

```python
manager.watch_directory("./plugins")

# Copy libphysics.dll into ./plugins/ at any point.
# It loads automatically under a readers-writer lock.
# The LLM sees the new tool on the next turn — no restart needed.
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
