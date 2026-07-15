#pragma once

#include <ue_sdk/types.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ue {

struct ProcessBackendState {
    void*    handle = nullptr; // HANDLE
    uint32_t pid    = 0;
};

struct ImageBackendState {
    const uint8_t* image      = nullptr;
    size_t         image_size = 0;
    uint64_t       image_base = 0;
    std::string    module_name;
};

// Fill process backend (Windows). Returns 0 on success.
int process_backend_open(uint32_t pid, ue_memory_backend* out, ProcessBackendState** state_out);
void process_backend_close(ue_memory_backend* backend, ProcessBackendState* state);

int image_backend_open(const void* image, size_t size, uint64_t base, const char* name,
                       ue_memory_backend* out, ImageBackendState** state_out);
void image_backend_close(ue_memory_backend* backend, ImageBackendState* state);

} // namespace ue
