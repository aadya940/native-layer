#pragma once

#include <string>
#include <stdexcept>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace OS {
inline std::string get_last_error() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return "No error";
    char buffer[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buffer, sizeof(buffer), NULL);
    return std::string(buffer);
#else
    const char* err = dlerror();
    return err ? std::string(err) : "Unknown OS error";
#endif
}

inline void* load_library(const std::string& path) {
#ifdef _WIN32
    void* handle = (void*)LoadLibraryA(path.c_str());
#else
    // RTLD_LAZY: resolve symbols as code is executed
    // RTLD_LOCAL: keep plugin symbols isolated from each other
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
    if (!handle) {
        throw std::runtime_error("Failed to load library: " + path + " - " + get_last_error());
    }
    return handle;
}

inline void* get_symbol(void* handle, const std::string& symbol_name) {
#ifdef _WIN32
    void* symbol = (void*)GetProcAddress((HMODULE)handle, symbol_name.c_str());
#else
    // Clear any existing errors first on POSIX
    dlerror(); 
    void* symbol = dlsym(handle, symbol_name.c_str());
#endif
    if (!symbol) {
        throw std::runtime_error("Failed to find symbol '" + symbol_name + "': " + get_last_error());
    }
    return symbol;
}

inline void close_library(void* handle) {
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}
} // namespace OS

