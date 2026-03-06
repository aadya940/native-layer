# Plugin Developer Guide

## Quick Start

1. Include `plugin_layer.h`
2. Implement `PluginAPI` structure
3. Export `get_plugin_api()` function
4. Compile as shared library

## Type System

### Scalars
- `TYPE_INT` + `DTYPE_I64`: 64-bit integer
- `TYPE_FLOAT` + `DTYPE_F64`: Double precision float

### Arrays
- `TYPE_BUFFER` + `DTYPE_F32`: float[] array
- `TYPE_BUFFER` + `DTYPE_F64`: double[] array
- `TYPE_BUFFER` + `DTYPE_I32`: int32_t[] array

### Strings
- `TYPE_STRING` + `DTYPE_BYTES`: UTF-8 text

## Validation Pattern

Every `execute()` implementation should:
```cpp
int execute(...) {
    // 1. Validate function name
    if (strcmp(fn, "my_function") != 0) return -1;
    
    // 2. Validate input count
    if (num_inputs != EXPECTED_COUNT) return -1;
    
    // 3. Validate type_id
    if (inputs[0].type_id != TYPE_BUFFER) return -1;
    
    // 4. Validate dtype
    if (inputs[0].dtype != DTYPE_F32) return -1;
    
    // 5. Perform operation
    // ...
    
    // 6. Set output correctly
    out->type_id = TYPE_FLOAT;
    out->dtype = DTYPE_F64;
    out->size = sizeof(double);
    
    return 0;
}
```

## Examples

See:
- `examples/plugin_simsimd.cpp` - C++ with SIMD
- `examples/plugin_rust/src/lib.rs` - Rust with serde_json
