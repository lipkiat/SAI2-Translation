#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>

// Owns an image mapped for editing, never for execution.
struct PE_IMAGE
{
    BYTE *buffer = nullptr;
    DWORD ImageSize = 0;
    UINT_PTR ImageBase = 0;

    PE_IMAGE() = default;
    ~PE_IMAGE();
    PE_IMAGE(const PE_IMAGE &) = delete;
    PE_IMAGE &operator=(const PE_IMAGE &) = delete;
};
using PE_HANDLE = PE_IMAGE *;

BOOL LoadPE(PE_HANDLE image, const BYTE *data, size_t size);
// The returned buffer must be released with std::free.
BYTE *SavePE(PE_HANDLE image, size_t *size);
void ClosePE(PE_HANDLE image);
BOOL LoadPEFile(PE_HANDLE image, const wchar_t *path);
BOOL SavePEFile(PE_HANDLE image, const wchar_t *path);

UINT_PTR ToBufferAddress(PE_HANDLE image, UINT_PTR address);
UINT_PTR ToVirtualAddress(PE_HANDLE image, UINT_PTR address);
