#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <string>
#include <stdexcept>
#include <memory>

#include "plugin_manager.cpp" 
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

    // Maps pybind11 buffer format chars to DTYPE_* constants.
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

        std::vector<MemoryBuffer>  mem_bufs(input_args.size());
        std::vector<py::buffer_info> buf_infos(input_args.size()); // keep alive
        // Track which slots were heap-allocated by the host (not py::buffer).
        std::vector<bool> host_owned(input_args.size(), false);

        // Pack each Python value into a MemoryBuffer.
        for (size_t i = 0; i < input_args.size(); ++i) {
            py::object arg = input_args[i];
            MemoryBuffer& mb = mem_bufs[i];
            mb.dtype = DTYPE_F64; // default; overwritten for TYPE_BUFFER

            if (py::isinstance<py::buffer>(arg)) {
                buf_infos[i] = arg.cast<py::buffer>().request();
                mb.data    = buf_infos[i].ptr;
                mb.size    = buf_infos[i].size * buf_infos[i].itemsize;
                mb.type_id = TYPE_BUFFER;
                mb.dtype   = dtype_from_format(buf_infos[i].format);
                // Python owns the memory — do NOT delete.
            }
            else if (py::isinstance<py::str>(arg)) {
                std::string s = arg.cast<std::string>();
                char* p = new char[s.length() + 1];
                std::strcpy(p, s.c_str());
                mb.data    = p;
                mb.size    = s.length();
                mb.type_id = TYPE_STRING;
                host_owned[i] = true;
            }
            else if (py::isinstance<py::int_>(arg)) {
                auto* p = new int64_t(arg.cast<int64_t>());
                mb.data    = p;
                mb.size    = sizeof(int64_t);
                mb.type_id = TYPE_INT;
                host_owned[i] = true;
            }
            else if (py::isinstance<py::float_>(arg)) {
                auto* p = new double(arg.cast<double>());
                mb.data    = p;
                mb.size    = sizeof(double);
                mb.type_id = TYPE_FLOAT;
                host_owned[i] = true;
            }
            else {
                throw std::invalid_argument(
                    "Unsupported type at input argument " + std::to_string(i));
            }
        }

        MemoryBuffer output_buf = {nullptr, 0, TYPE_UNKNOWN, DTYPE_F64};

        // Free all host-allocated inputs (not py::buffer ones).
        auto free_inputs = [&]() {
            for (size_t i = 0; i < mem_bufs.size(); ++i) {
                if (!host_owned[i]) continue;
                if (mem_bufs[i].type_id == TYPE_STRING)
                    delete[] static_cast<char*>(mem_bufs[i].data);
                else if (mem_bufs[i].type_id == TYPE_INT)
                    delete static_cast<int64_t*>(mem_bufs[i].data);
                else if (mem_bufs[i].type_id == TYPE_FLOAT)
                    delete static_cast<double*>(mem_bufs[i].data);
            }
        };

        // Drop the GIL and execute C++.
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

        // Decode the output back to Python.
        py::object result = py::none();

        if (output_buf.data != nullptr) {
            if (output_buf.type_id == TYPE_BUFFER) {
                py::list ret;
                switch (output_buf.dtype) {
                    case DTYPE_F32: {
                        size_t n = output_buf.size / sizeof(float);
                        auto*  d = static_cast<float*>(output_buf.data);
                        for (size_t i = 0; i < n; ++i) ret.append((double)d[i]);
                        break;
                    }
                    case DTYPE_I64: {
                        size_t n = output_buf.size / sizeof(int64_t);
                        auto*  d = static_cast<int64_t*>(output_buf.data);
                        for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                        break;
                    }
                    case DTYPE_I32: {
                        size_t n = output_buf.size / sizeof(int32_t);
                        auto*  d = static_cast<int32_t*>(output_buf.data);
                        for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                        break;
                    }
                    case DTYPE_BYTES:
                        result = py::bytes(static_cast<const char*>(output_buf.data),
                                           output_buf.size);
                        break;
                    default: { // DTYPE_F64
                        size_t n = output_buf.size / sizeof(double);
                        auto*  d = static_cast<double*>(output_buf.data);
                        for (size_t i = 0; i < n; ++i) ret.append(d[i]);
                        break;
                    }
                }
                if (output_buf.dtype != DTYPE_BYTES) result = ret;
            }
            else if (output_buf.type_id == TYPE_STRING) {
                result = py::str(static_cast<const char*>(output_buf.data),
                                 output_buf.size);
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
        // We pass a lambda that ties the OS Watcher to the Memory Manager
        watcher = std::make_unique<DirectoryWatcher>(path, 
            [this](const std::string& plugin, const std::string& shadow_path) {
                try {
                    // The manager handles its own C++ thread-safety via the shared_mutex
                    this->manager.load_plugin(plugin, shadow_path);
                    std::cout << "Hot-reloaded native tool: " << plugin << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to hot-reload " << plugin << ": " << e.what() << std::endl;
                }
            }
        );
        watcher->start();
    }

    void stop_watching() {
        if (watcher) {
            watcher->stop();
            watcher.reset();
        }
    }

    py::list get_active_tools() {
        py::list tool_names;
        std::vector<std::string> current_plugins = manager.get_loaded_plugins();
        
        for (const std::string& name : current_plugins) {
            tool_names.append(name);
        }
        
        return tool_names;
    }
};

PYBIND11_MODULE(_core, m) {
    m.doc() = "LLM Agent Tool interface that lets you use compiled programming languages like C/C++/Rust/Zig etc with zero setup.";

    py::class_<NativeManagerWrapper>(m, "NativeManager")
        .def(py::init<>())
        .def("load_plugin", &NativeManagerWrapper::load_plugin)
        .def("get_schema", &NativeManagerWrapper::get_schema)
        .def("execute", &NativeManagerWrapper::execute)
        .def("watch_directory", &NativeManagerWrapper::watch_directory)
        .def("stop_watching", &NativeManagerWrapper::stop_watching)
        .def("get_active_tools", &NativeManagerWrapper::get_active_tools);
}
