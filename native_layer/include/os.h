#pragma once

#include <string>
#include <functional>
#include <stdexcept>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <sys/mman.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #include <cstring>
#endif

namespace OS {

struct ProcessResult {
    int exit_code;
    bool crashed;
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

#ifndef _WIN32

// Shared memory header
struct ShmHeader {
    size_t size;
    uint32_t type_id;
    uint32_t dtype;
};

constexpr size_t SHM_MAX_SIZE = 1000 * 1024 * 1024; // 1GB

inline ProcessResult run_isolated(
    std::function<int(void*, size_t&)> child_exec,
    void* out_buffer,
    size_t& out_size,
    uint32_t& type_id,
    uint32_t& dtype
) {

    size_t total_size = sizeof(ShmHeader) + SHM_MAX_SIZE;

    void* shm = mmap(
        nullptr,
        total_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANON,
        -1,
        0
    );

    if (shm == MAP_FAILED)
        throw std::runtime_error("mmap failed");

    fflush(nullptr);

    pid_t pid = fork();

    if (pid < 0)
        throw std::runtime_error("fork failed");

    if (pid == 0) {

        auto* header = (ShmHeader*)shm;
        void* data   = (char*)shm + sizeof(ShmHeader);

        size_t size = 0;
        int status = child_exec(data, size);

        header->size = size;
        header->type_id = type_id;
        header->dtype = dtype;

        _exit(status);
    }

    int status;
    waitpid(pid, &status, 0);

    ProcessResult res;

    if (WIFSIGNALED(status)) {
        res.crashed = true;
        res.exit_code = WTERMSIG(status);
    } else {
        res.crashed = false;
        res.exit_code = WEXITSTATUS(status);
    }

    if (!res.crashed) {
        auto* header = (ShmHeader*)shm;
        void* data   = (char*)shm + sizeof(ShmHeader);

        out_size = header->size;
        type_id  = header->type_id;
        dtype    = header->dtype;

        memcpy(out_buffer, data, header->size);
    }

    munmap(shm, total_size);

    return res;
}

#else

struct IsolatedExec {
    std::function<int()>* fn;
    static int call(void* ctx) {
        return (*static_cast<IsolatedExec*>(ctx)->fn)();
    }
};

static ProcessResult run_isolated_seh(int(*exec)(void*), void* ctx) {
    ProcessResult result;
    __try {
        result.exit_code = exec(ctx);
        result.crashed = false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result.exit_code = -1;
        result.crashed = true;
    }
    return result;
}

inline ProcessResult run_isolated(std::function<int()> exec) {
    IsolatedExec ctx{ &exec };
    return run_isolated_seh(&IsolatedExec::call, &ctx);
}

#endif

} // namespace OS
