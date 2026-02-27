#include "plugin_layer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT
#endif

static const char* get_schema() {
    return R"([
        {
            "name": "double_array",
            "description": "Doubles every element of an array.",
            "parameters": {
                "type": "object",
                "properties": {
                    "data": { "type": "array", "items": { "type": "number" } }
                },
                "required": ["data"]
            }
        },
        {
            "name": "scale_array",
            "description": "Multiplies each element of an array by a scalar.",
            "parameters": {
                "type": "object",
                "properties": {
                    "data": { "type": "array", "items": { "type": "number" } },
                    "scalar": { "type": "number" }
                },
                "required": ["data", "scalar"]
            }
        },
        {
            "name": "add_arrays",
            "description": "Element-wise addition of two arrays.",
            "parameters": {
                "type": "object",
                "properties": {
                    "a": { "type": "array", "items": { "type": "number" } },
                    "b": { "type": "array", "items": { "type": "number" } }
                },
                "required": ["a", "b"]
            }
        }
    ])";
}

static int execute(const char* fn, const MemoryBuffer* inputs, size_t n, MemoryBuffer* out) {
    if (strcmp(fn, "double_array") == 0) {
        if (n < 1) return -1;
        double* data = (double*)inputs[0].data;
        size_t count = inputs[0].size / sizeof(double);
        double* result = (double*)malloc(count * sizeof(double));
        for (size_t i = 0; i < count; i++) result[i] = data[i] * 2.0;
        out->data = result;
        out->size = count * sizeof(double);
        out->type_id = TYPE_BUFFER;
        out->dtype = DTYPE_F64;
        return 0;
    } 
    else if (strcmp(fn, "scale_array") == 0) {
        if (n < 2) return -1;
        double* data = (double*)inputs[0].data;
        size_t count = inputs[0].size / sizeof(double);
        
        double scalar = 0.0;
        if (inputs[1].type_id == TYPE_INT) {
            scalar = (double)(*(int64_t*)inputs[1].data);
        } else if (inputs[1].type_id == TYPE_FLOAT) {
            scalar = *(double*)inputs[1].data;
        } else {
            return -1;
        }

        double* result = (double*)malloc(count * sizeof(double));
        for (size_t i = 0; i < count; i++) result[i] = data[i] * scalar;
        out->data = result;
        out->size = count * sizeof(double);
        out->type_id = TYPE_BUFFER;
        out->dtype = DTYPE_F64;
        return 0;
    }
    else if (strcmp(fn, "add_arrays") == 0) {
        if (n < 2) return -1;
        double* a = (double*)inputs[0].data;
        double* b = (double*)inputs[1].data;
        size_t count_a = inputs[0].size / sizeof(double);
        size_t count_b = inputs[1].size / sizeof(double);
        if (count_a != count_b) return -1;
        double* result = (double*)malloc(count_a * sizeof(double));
        for (size_t i = 0; i < count_a; i++) result[i] = a[i] + b[i];
        out->data = result;
        out->size = count_a * sizeof(double);
        out->type_id = TYPE_BUFFER;
        out->dtype = DTYPE_F64;
        return 0;
    }
    return -1;
}

static void free_buffer(MemoryBuffer* b) { free(b->data); }
static int  init(const char* cfg)        { return 0; }
static void shut()                       {}

static PluginAPI api = {
    "math", "1.0",
    get_schema, init, shut, execute, free_buffer
};

extern "C" EXPORT PluginAPI* get_plugin_api() { return &api; }
