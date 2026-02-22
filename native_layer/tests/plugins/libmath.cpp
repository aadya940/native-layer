#include "../../../include/plugin_layer.h"
#include <cstring>

#ifdef _WIN32
    #define EXPORT_SYMBOL __declspec(dllexport)
#else
    #define EXPORT_SYMBOL
#endif

extern "C" {

const char* get_schema() {
    return R"([
        {
            "name": "double_array",
            "description": "Doubles every element of a float64 array.",
            "parameters": {
                "type": "object",
                "properties": {
                    "data": { "type": "array", "items": { "type": "number" } }
                }
            }
        },
        {
            "name": "scale_array",
            "description": "Multiplies each element by a scalar.",
            "parameters": {
                "type": "object",
                "properties": {
                    "data":   { "type": "array",  "items": { "type": "number" } },
                    "scalar": { "type": "number" }
                }
            }
        },
        {
            "name": "add_arrays",
            "description": "Element-wise addition of two float64 arrays.",
            "parameters": {
                "type": "object",
                "properties": {
                    "a": { "type": "array", "items": { "type": "number" } },
                    "b": { "type": "array", "items": { "type": "number" } }
                }
            }
        }
    ])";
}

int initialize(const char* config) { return 0; }
void shutdown() {}

int execute(const char* fn, const MemoryBuffer* in, size_t n, MemoryBuffer* out) {

    auto make_out = [&](double* data, size_t count) {
        out->data    = data;
        out->size    = count * sizeof(double);
        out->type_id = TYPE_BUFFER;
        out->dtype   = DTYPE_F64;
    };

    if (strcmp(fn, "double_array") == 0) {
        if (n < 1 || in[0].type_id != TYPE_BUFFER) return -1;
        size_t cnt = in[0].size / sizeof(double);
        double* a  = (double*)in[0].data;
        double* r  = new double[cnt];
        for (size_t i = 0; i < cnt; ++i) r[i] = a[i] * 2.0;
        make_out(r, cnt);
        return 0;
    }

    if (strcmp(fn, "scale_array") == 0) {
        if (n < 2 || in[0].type_id != TYPE_BUFFER) return -1;
        double scalar = (in[1].type_id == TYPE_FLOAT)
            ? *(double*)in[1].data
            : (double)*(int64_t*)in[1].data;
        size_t cnt = in[0].size / sizeof(double);
        double* a  = (double*)in[0].data;
        double* r  = new double[cnt];
        for (size_t i = 0; i < cnt; ++i) r[i] = a[i] * scalar;
        make_out(r, cnt);
        return 0;
    }

    if (strcmp(fn, "add_arrays") == 0) {
        if (n < 2 || in[0].type_id != TYPE_BUFFER || in[1].type_id != TYPE_BUFFER) return -1;
        size_t ca = in[0].size / sizeof(double);
        size_t cb = in[1].size / sizeof(double);
        if (ca != cb) return -1;
        double* a = (double*)in[0].data;
        double* b = (double*)in[1].data;
        double* r = new double[ca];
        for (size_t i = 0; i < ca; ++i) r[i] = a[i] + b[i];
        make_out(r, ca);
        return 0;
    }

    return -1;
}

void free_buffer(MemoryBuffer* buf) {
    delete[] (double*)buf->data;
    buf->data = nullptr;
}

PluginAPI api = { "math_engine", "1.1.0", get_schema, initialize, shutdown, execute, free_buffer };

EXPORT_SYMBOL PluginAPI* get_plugin_api() { return &api; }

} // extern "C"
