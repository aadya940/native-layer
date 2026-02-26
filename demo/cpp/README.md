# C++ SIMD Plugin Demo

This demo shows how to offload heavy vector math to a native C++ plugin using SIMD (simSIMD) while keeping the agent logic in Python.

## Prerequisites

- **Python 3.10+**
- **MSVC (Visual Studio)**: For the `cl` compiler (Windows).
- **Google AI API Key**: Set in a `.env` file.

## Setup

1. **Download Dependencies**:
   Run the script to fetch the header-only `simsimd.h`:
   ```bash
   python download_simsimd.py
   ```

2. **Compile the Plugin**:
   Open a "Developer Command Prompt for VS" and run:
   ```bash
   cl /LD /EHsc /O2 plugin.cpp /Fe:plugin_simsimd.dll
   ```

## Running the Agent

The agent will automatically detect the compiled `.dll` when it's moved to the `/plugins` directory (the script handles this).

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

## Files

- `plugin.cpp`: C++ source for the SIMD tool.
- `agent_adk.py`: Agent entry point.
- `download_simsimd.py`: Utility to get `simsimd.h`.
- `test_plugin.py`: Simple test script for the native layer integration.
