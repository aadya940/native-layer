#pragma once

/* This file implements the exact memory contract (Stable ABI) that both the 
   host and the plugin will use to communicate with each other.
*/

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type Definitions
   These will be used to cast void* pointer to the appropriate type.
*/
#define TYPE_UNKNOWN 0
#define TYPE_BUFFER 1 // Array of numeric elements (dtype specifies element type)
#define TYPE_STRING 2 // UTF-8 string
#define TYPE_INT    3 // 64-bit integer scalar
#define TYPE_FLOAT  4 // 64-bit float (double) scalar

/* Buffer element dtype — only meaningful when type_id == TYPE_BUFFER.
   Zero-default (DTYPE_F64) preserves backwards compatibility with existing plugins. */
#define DTYPE_F64   0  /* double   — default */
#define DTYPE_F32   1  /* float              */
#define DTYPE_I64   2  /* int64_t            */
#define DTYPE_I32   3  /* int32_t            */
#define DTYPE_BYTES 4  /* opaque bytes       */


/* Memory Buffer is the C-Struct to implement zero copy interface
   with C style alignment across all compilers. */
typedef struct {
    void*    data;      // Raw pointer to the physical memory address.
    size_t   size;      // Size of the buffer in bytes.
    uint32_t type_id;   // Buffer category (TYPE_*).
    uint32_t dtype;     // Element type for TYPE_BUFFER buffers (DTYPE_*).
} MemoryBuffer;


/* Plugin API.
   This struct will implement an OS like pointer table to implement dynamic dispatch.
    The host manager resolves this single struct to discover all capabilities
    without paying the penalty of multiple string-based `dlsym` lookups.
*/
typedef struct {
    const char* name;
    const char* version;

    // Returns the JSON String defining the tools for the LLM.
    const char* (*get_schema)();

    // Lifecycle Hooks
    int (*initialize)(const char* config);
    void (*shutdown)();

    // Execution Layer
    // Returns 0 on success, non-zero on failure or error.
    int (*execute)(const char* function_name,
        const MemoryBuffer* inputs,
        size_t num_inputs,
        MemoryBuffer* output
    );

    // Memory Ownership: To be freed by the Plugins allocator.
    void (*free_buffer)(MemoryBuffer* buffer);
} PluginAPI;

// Export
/**
 * Every compiled .so file MUST export this exact symbol.
 * The host manager will call this to retrieve the function pointer table.
 */
typedef PluginAPI* (*GetPluginAPIFunc)();


#ifdef __cplusplus
}
#endif
