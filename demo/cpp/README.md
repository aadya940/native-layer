# C++ SIMD Plugin Demo

This demo shows how to offload heavy vector math to a native C++ plugin using SIMD (simSIMD) while keeping the agent logic in Python.

## Prerequisites

- **Python 3.11+**
- **A C++ compiler**:
  - Windows: MSVC (Visual Studio)
  - macOS: clang++
  - Linux: g++ or clang++
- **API keys**:
  - `agent_adk.py` (Google ADK + Gemini) requires a Google AI API key in `.env`.
  - `agent_langchain.py` does not call an LLM and does not require API keys.

## Setup

1. **Download Dependencies**:
   Run the script to fetch the header-only `simsimd.h`:
   ```bash
   python download_simsimd.py
   ```

2. **Compile the Plugin**:
   Use the command for your OS (see `Commands.txt`):

   Windows (MSVC):
   ```bash
   cl /LD /EHsc /O2 plugin.cpp /Fe:plugin_simsimd.dll
   ```

   macOS:
   ```bash
   clang++ -shared -fPIC -O2 -std=c++17 -undefined dynamic_lookup plugin.cpp -o plugin_simsimd.dylib
   ```

   Linux (g++):
   ```bash
   g++ -shared -fPIC -O2 -std=c++17 plugin.cpp -o plugin_simsimd.so
   ```

   Linux (clang++):
   ```bash
   clang++ -shared -fPIC -O2 -std=c++17 plugin.cpp -o plugin_simsimd.so
   ```

   If you see `cl : The term 'cl' is not recognized`, open a "Developer Command Prompt for VS" (or run from a shell that has MSVC on PATH).

## Running the Agent

The agent will automatically detect the compiled plugin when it's moved to the `/plugins` directory (the script handles this).

LangChain (no API keys required):
```bash
python agent_langchain.py
```

Google ADK + Gemini (requires `.env` API key):
```bash
python agent_adk.py
```

## How It Works

1. **`plugin.cpp`**: Implements the `PluginAPI`. It uses `simsimd_cos_f32_serial` to calculate cosine similarity via SIMD.
2. **`agent_adk.py`**:
   - Initialises `NativeManager` to watch the `./plugins` folder.
   - Wraps the native tool in a `NativeADKToolset`.
   - Passes the toolset to a Gemini-powered `Agent`.
   - The agent calls the C++ function directly when asked about vector similarity.

3. **`agent_langchain.py`**:
   - Initialises `NativeManager` to watch the `./plugins` folder.
   - Uses `NativeHotReloadMiddleware` to materialize LangChain `StructuredTool`s.
   - Invokes `plugin_simsimd_cosine_similarity` directly (no LLM required).

## Files

- `plugin.cpp`: C++ source for the SIMD tool.
- `agent_adk.py`: Agent entry point.
- `agent_langchain.py`: LangChain entry point (tool invocation without an LLM).
- `download_simsimd.py`: Utility to get `simsimd.h`.
- `test_plugin.py`: Simple test script for the native layer integration.
