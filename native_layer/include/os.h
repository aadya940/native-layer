#pragma once

#include <string>
#include <stdexcept>
#include <functional>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #include <sys/wait.h>
#endif

namespace OS {

struct ProcessResult {
    int exit_code = 0;
    bool crashed = false;
};

using ChildFunc = std::function<int()>;

inline std::string get_last_error() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return "No error";

    char buffer[256];
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        sizeof(buffer),
        NULL
    );

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
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    if (!handle) {
        throw std::runtime_error(
            "Failed to load library: " + path + " - " + get_last_error()
        );
    }

    return handle;
}

inline void* get_symbol(void* handle, const std::string& symbol_name) {
#ifdef _WIN32
    void* symbol = (void*)GetProcAddress((HMODULE)handle, symbol_name.c_str());
#else
    dlerror(); // clear previous errors
    void* symbol = dlsym(handle, symbol_name.c_str());
#endif

    if (!symbol) {
        throw std::runtime_error(
            "Failed to find symbol '" + symbol_name + "': " + get_last_error()
        );
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

inline ProcessResult run_isolated(ChildFunc func) {

#ifdef _WIN32

    // Windows: structured exception protection
    ProcessResult result;

    __try {
        result.exit_code = func();
        result.crashed = false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result.crashed = true;
        result.exit_code = -1;
    }

    return result;

#else

    pid_t pid = fork();

    if (pid < 0) {
        throw std::runtime_error("fork() failed");
    }

    if (pid == 0) {
        int code = 0;

        try {
            code = func();
        } catch (...) {
            code = -1;
        }

        _exit(code);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    ProcessResult result;

    if (WIFSIGNALED(status)) {
        result.crashed = true;
        result.exit_code = WTERMSIG(status);
    } else {
        result.crashed = false;
        result.exit_code = WEXITSTATUS(status);
    }

    return result;

#endif
}

} // namespace OS
