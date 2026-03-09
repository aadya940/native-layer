# Rust Native Plugin Demo

This demo mirrors `demo/cpp`, but implements a native plugin in Rust.

It uses the ABI contract defined in:

- `native_layer/include/plugin_layer.h`

and imports the matching Rust binding module:

- `native_layer/include/bindings/plugin_layer.rs`

## What it demos

Uses `serde_json` to provide two native tools:

- `json_pretty`: pretty-print a JSON string
- `json_get`: extract a value from JSON using a dot-separated path (e.g. `a.b.c`)

## Prerequisites

- Python 3.11+
- Rust toolchain (stable) + Cargo
- API keys:
  - This demo does not call an LLM and does not require API keys.

## Build

From `demo/rust/`:

```bash
cargo build --release
```

## Run

```bash
python agent_langchain.py
```

The script:

- Watches `./plugins`
- Copies the compiled Rust shared library into `./plugins`
- Uses `NativeHotReloadMiddleware` to create LangChain `StructuredTool`s
- Invokes `plugin_rust_json_pretty` and `plugin_rust_json_get`
