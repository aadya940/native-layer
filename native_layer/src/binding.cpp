#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <cstring>

#include "../include/plugin_manager.h" 
#include "../include/watcher.h"

namespace py = pybind11;

class NativeManagerWrapper {
private:
    PluginManager manager;
    std::unique_ptr<DirectoryWatcher> watcher;

public:
    NativeManagerWrapper() = default;

    void load_plugin(const std::string& name, const std::string& path) {
        manager.load_plugin(name, path);
    }

    std::string get_schema(const std::string& name) {
        return manager.get_schema(name);
    }

    static uint32_t dtype_from_format(const std::string& fmt) {
        if (fmt == "d") return DTYPE_F64;
        if (fmt == "f") return DTYPE_F32;
        if (fmt == "l" || fmt == "q" || fmt == "L" || fmt == "Q") return DTYPE_I64;
        if (fmt == "i" || fmt == "I") return DTYPE_I32;
        return DTYPE_BYTES;
    }

    py::object execute(const std::string& plugin,
                       const std::string& func,
                       py::list input_args) {

        std::vector<MemoryBuffer> mem_bufs(input_args.size());
        std::vector<py::buffer_info> buf_infos(input_args.size());
        std::vector<bool> host_owned(input_args.size(), false);

        // Pack Python values into MemoryBuffers
        for (size_t i = 0; i < input_args.size(); ++i) {
            py::object arg = input_args[i];
            MemoryBuffer& mb = mem_bufs[i];
            
            // Initialize shape/ndim (NEW)
            mb.shape = nullptr;
            mb.ndim = 0;
            mb.dtype = DTYPE_F64;

            if (py::isinstance<py::buffer>(arg)) {
                buf_infos[i] = arg.cast<py::buffer>().request();
                mb.type_id = TYPE_BUFFER;

                // Handle shape (NEW)
                mb.ndim = static_cast<uint32_t>(buf_infos[i].ndim);
                if (mb.ndim > 0) {
                    mb.shape = new size_t[mb.ndim];
                    for (uint32_t d = 0; d < mb.ndim; ++d) {
                        mb.shape[d] = static_cast<size_t>(buf_infos[i].shape[d]);
                    }
                }

                if (buf_infos[i].format == "f") {
                    // Convert f32 -> f64
                    size_t n = static_cast<size_t>(buf_infos[i].size);
                    auto* converted = new double[n];
                    auto* src = static_cast<const float*>(buf_infos[i].ptr);
                    for (size_t j = 0; j < n; ++j) converted[j] = static_cast<double>(src[j]);
                    mb.data = converted;
                    mb.size = n * sizeof(double);
                    mb.dtype = DTYPE_F64;
                    host_owned[i] = true;
                } else {
                    mb.data = buf_infos[i].ptr;
                    mb.size = buf_infos[i].size * buf_infos[i].itemsize;
                    mb.dtype = dtype_from_format(buf_infos[i].format);
                }
            }
            else if (py::isinstance<py::str>(arg)) {
                std::string s = arg.cast<std::string>();
                char* p = new char[s.length() + 1];
                std::strcpy(p, s.c_str());
                mb.data = p;
                mb.size = s.length();
                mb.type_id = TYPE_STRING;
                host_owned[i] = true;
            }
            else if (py::isinstance<py::int_>(arg)) {
                auto* p = new int64_t(arg.cast<int64_t>());
                mb.data = p;
                mb.size = sizeof(int64_t);
                mb.type_id = TYPE_INT;
                host_owned[i] = true;
            }
            else if (py::isinstance<py::float_>(arg)) {
                auto* p = new double(arg.cast<double>());
                mb.data = p;
                mb.size = sizeof(double);
                mb.type_id = TYPE_FLOAT;
                host_owned[i] = true;
            }
            else {
                throw std::invalid_argument(
                    "Unsupported type at input argument " + std::to_string(i));
            }
        }

        // Initialize output buffer (NEW - set shape/ndim)
        MemoryBuffer output_buf = {
            nullptr,        // data
            0,              // size
            TYPE_UNKNOWN,   // type_id
            DTYPE_F64,      // dtype
            nullptr,        // shape (NEW)
            0               // ndim (NEW)
        };

        // Free host-allocated inputs (NEW - also free shapes)
        auto free_inputs = [&]() {
            for (size_t i = 0; i < mem_bufs.size(); ++i) {
                // Free shape if allocated by host
                if (mem_bufs[i].shape && host_owned[i]) {
                    delete[] mem_bufs[i].shape;
                }
                
                if (!host_owned[i]) continue;
                
                if (mem_bufs[i].type_id == TYPE_STRING)
                    delete[] static_cast<char*>(mem_bufs[i].data);
                else if (mem_bufs[i].type_id == TYPE_BUFFER)
                    delete[] static_cast<double*>(mem_bufs[i].data);
                else if (mem_bufs[i].type_id == TYPE_INT)
                    delete static_cast<int64_t*>(mem_bufs[i].data);
                else if (mem_bufs[i].type_id == TYPE_FLOAT)
                    delete static_cast<double*>(mem_bufs[i].data);
            }
        };

        // Execute plugin
        try {
            py::gil_scoped_release release;
            manager.execute(plugin, func,
                          mem_bufs.data(), mem_bufs.size(),
                          &output_buf);
        } catch (...) {
            manager.free_buffer(plugin, &output_buf);
            free_inputs();
            throw;
        }

        // Decode output back to Python (NEW - handle shape for numpy arrays)
        py::object result = py::none();

        if (output_buf.data != nullptr) {
            if (output_buf.type_id == TYPE_BUFFER) {
                // Check if we have shape info (NEW)
                if (output_buf.shape && output_buf.ndim > 0) {
                    // Return as numpy array with proper shape
                    std::vector<ssize_t> shape(output_buf.ndim);
                    for (uint32_t i = 0; i < output_buf.ndim; ++i) {
                        shape[i] = static_cast<ssize_t>(output_buf.shape[i]);
                    }
                    
                    switch (output_buf.dtype) {
                        case DTYPE_F64:
                            result = py::array_t<double>(shape, static_cast<double*>(output_buf.data));
                            break;
                        case DTYPE_F32:
                            result = py::array_t<float>(shape, static_cast<float*>(output_buf.data));
                            break;
                        case DTYPE_I64:
                            result = py::array_t<int64_t>(shape, static_cast<int64_t*>(output_buf.data));
                            break;
                        case DTYPE_I32:
                            result = py::array_t<int32_t>(shape, static_cast<int32_t*>(output_buf.data));
                            break;
                        default:
                            result = py::bytes(static_cast<const char*>(output_buf.data), output_buf.size);
                    }
                } else {
                    // No shape - return as list (backward compatible)
                    py::list ret;
                    switch (output_buf.dtype) {
                        case DTYPE_F32: {
                            size_t n = output_buf.size / sizeof(float);
                            auto* d = static_cast<float*>(output_buf.data);
                            for (size_t i = 0; i < n; ++i) ret.append((double)d[i]);
                            break;
                        }
                        case DTYPE_I64: {
                            size_t n = output_buf.size / sizeof(int64_t);
                            auto* d = static_cast<int64_t*>(output_buf.data);
                            for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                            break;
                        }
                        case DTYPE_I32: {
                            size_t n = output_buf.size / sizeof(int32_t);
                            auto* d = static_cast<int32_t*>(output_buf.data);
                            for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                            break;
                        }
                        case DTYPE_BYTES:
                            result = py::bytes(static_cast<const char*>(output_buf.data), output_buf.size);
                            break;
                        default: { // DTYPE_F64
                            size_t n = output_buf.size / sizeof(double);
                            auto* d = static_cast<double*>(output_buf.data);
                            for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                            break;
                        }
                    }
                    if (output_buf.dtype != DTYPE_BYTES) result = ret;
                }
            }
            else if (output_buf.type_id == TYPE_STRING) {
                result = py::str(static_cast<const char*>(output_buf.data), output_buf.size);
            }
            else if (output_buf.type_id == TYPE_INT) {
                result = py::int_(*static_cast<int64_t*>(output_buf.data));
            }
            else if (output_buf.type_id == TYPE_FLOAT) {
                result = py::float_(*static_cast<double*>(output_buf.data));
            }
        }

        manager.free_buffer(plugin, &output_buf);
        free_inputs();
        return result;
    }

    void watch_directory(const std::string& path) {
        watcher = std::make_unique<DirectoryWatcher>(path, 
            [this](const std::string& plugin, const std::string& shadow_path) {
                try {
                    this->manager.load_plugin(plugin, shadow_path);
                    std::cout << "Hot-reloaded: " << plugin << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to reload " << plugin << ": " << e.what() << std::endl;
                }
            }
        );
        watcher->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void stop_watching() {
        if (watcher) {
            watcher->stop();
            watcher.reset();
        }
    }

    py::list get_active_tools() {
        py::list tool_names;
        for (const std::string& name : manager.get_loaded_plugins()) {
            tool_names.append(name);
        }
        return tool_names;
    }
};

PYBIND11_MODULE(native_plugin_layer, m) {
    m.doc() = "Native tool layer for LLM agents";

    py::class_<NativeManagerWrapper>(m, "NativeManager")
        .def(py::init<>())
        .def("load_plugin", &NativeManagerWrapper::load_plugin)
        .def("get_schema", &NativeManagerWrapper::get_schema)
        .def("execute", &NativeManagerWrapper::execute)
        .def("watch_directory", &NativeManagerWrapper::watch_directory)
        .def("stop_watching", &NativeManagerWrapper::stop_watching)
        .def("get_active_tools", &NativeManagerWrapper::get_active_tools);
}
