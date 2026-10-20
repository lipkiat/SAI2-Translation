#pragma once
#include <cstdint>

// Owns an image mapped for editing, never for execution.
struct PE_IMAGE
{
    uint8_t *buffer = nullptr;
    uint32_t ImageSize = 0;
    uintptr_t ImageBase = 0;

    PE_IMAGE() = default;
    ~PE_IMAGE();
    PE_IMAGE(const PE_IMAGE &) = delete;
    PE_IMAGE &operator=(const PE_IMAGE &) = delete;
};
using PE_HANDLE = PE_IMAGE *;

bool LoadPE(PE_HANDLE image, const uint8_t *data, size_t size);
// The returned buffer must be released with std::free.
uint8_t *SavePE(PE_HANDLE image, size_t *size);
void ClosePE(PE_HANDLE image);

bool LoadPEFile(PE_HANDLE image, const wchar_t *path);
bool SavePEFile(PE_HANDLE image, const wchar_t *path);

uintptr_t ToBufferAddress(PE_HANDLE image, uintptr_t address);
uintptr_t ToVirtualAddress(PE_HANDLE image, uintptr_t address);
