/*
Example of a plugin that can be loaded by the native layer.
We will use the high performance SIMD simSIMD library to calculate 
the cosine similarity of two vectors using an AI Agent.
*/

#include "../../native_layer/include/plugin_layer.h"

#pragma warning(disable : 4068)

#define SIMSIMD_NATIVE_F16 0
#include "simsimd.h"

#include <string.h>
#include <stdlib.h>
 #include <vector>

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT
#endif

static const char* get_schema() {
    return R"({
        "name": "cosine_similarity",
        "description": "Calculates cosine similarity of two vectors.",
        "parameters": {
            "type": "object",
            "properties": {
                "vec_a": { "type": "array", "items": { "type": "number", "format": "float32" } },
                "vec_b": { "type": "array", "items": { "type": "number", "format": "float32" } }
            },
            "required": ["vec_a", "vec_b"]
        }
    })";
}

static int execute(const char* fn, const MemoryBuffer* inputs, size_t n, MemoryBuffer* out) {
    if (strcmp(fn, "cosine_similarity") != 0) return -1;

    // 1. Cast/convert input pointers.
    // The host bridge may pass float32 buffers (DTYPE_F32) or converted float64 buffers (DTYPE_F64).
    const bool a_is_f64 = inputs[0].dtype == DTYPE_F64;
    const bool b_is_f64 = inputs[1].dtype == DTYPE_F64;

    size_t dim = a_is_f64 ? (inputs[0].size / sizeof(double)) : (inputs[0].size / sizeof(float));

    simsimd_f32_t* a = nullptr;
    simsimd_f32_t* b = nullptr;
    simsimd_f32_t* a_tmp = nullptr;
    simsimd_f32_t* b_tmp = nullptr;

    if (a_is_f64) {
        const double* src = (const double*)inputs[0].data;
        a_tmp = (simsimd_f32_t*)malloc(dim * sizeof(simsimd_f32_t));
        for (size_t i = 0; i < dim; ++i) a_tmp[i] = (simsimd_f32_t)src[i];
        a = a_tmp;
    } else {
        a = (simsimd_f32_t*)inputs[0].data;
    }

    if (b_is_f64) {
        const double* src = (const double*)inputs[1].data;
        b_tmp = (simsimd_f32_t*)malloc(dim * sizeof(simsimd_f32_t));
        for (size_t i = 0; i < dim; ++i) b_tmp[i] = (simsimd_f32_t)src[i];
        b = b_tmp;
    } else {
        b = (simsimd_f32_t*)inputs[1].data;
    }

    simsimd_distance_t distance;
    simsimd_cos_f32_serial(a, b, dim, &distance);
    
    double similarity = 1.0 - distance;

    // 3. Return Result
    double* res = (double*)malloc(sizeof(double));
    *res = similarity;

    out->data = res;
    out->size = sizeof(double);
    out->type_id = TYPE_FLOAT;
    out->dtype = DTYPE_F64;

    if (a_tmp) free(a_tmp);
    if (b_tmp) free(b_tmp);
    return 0;
}

static void free_buffer(MemoryBuffer* b) { free(b->data); }
static int init(const char* c) { return 0; }
static void shut() {}

static PluginAPI api = { "plugin_simsimd", "1.0", get_schema, init, shut, execute, free_buffer };
extern "C" EXPORT PluginAPI* get_plugin_api() { return &api; }
