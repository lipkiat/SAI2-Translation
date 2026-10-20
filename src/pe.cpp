#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include "pe.h"


static void* LoadFileData(const wchar_t* path, size_t* size)
{
	HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		return nullptr;
	}
    LARGE_INTEGER fileSize;
    DWORD bytesRead;
    size_t remainingSize;
    void* data = nullptr;
    char* ptr;
    const size_t chunkSize = 1 << 31; // 2GB
    DWORD currentChunkSize;
	do {
		if (!GetFileSizeEx(hFile, &fileSize)) {
			break;
		}
		remainingSize = (size_t)fileSize.QuadPart;
		*size = remainingSize;
		data = std::malloc(remainingSize);
		if (!data) {
			break;
		}
		ptr = (char*)data;
		while (remainingSize > 0) {
			currentChunkSize = static_cast<DWORD>(min(chunkSize, remainingSize));
			if (!ReadFile(hFile, ptr, currentChunkSize, &bytesRead, nullptr)) {
				std::free(data);
				data = nullptr;
				break;
			}
			ptr += bytesRead;
			remainingSize -= bytesRead;
		}
	} while(false);
	CloseHandle(hFile);
	return data;
}

static bool SaveFileData(const wchar_t* path, void* data, size_t size)
{
	HANDLE hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
	DWORD written;
    size_t offset = 0;
    bool r = true;
    while (offset < size)
    {
        const DWORD chunk = static_cast<DWORD>(min(size - offset, 1 << 31));
        r = WriteFile(hFile, static_cast<BYTE*>(data) + offset, chunk, &written, nullptr);
        if (!r)
            break;
        offset += written;
    }
	CloseHandle(hFile);
	return r;
}


static bool Contains(size_t total, size_t offset, size_t length)
{
    return offset <= total && length <= total - offset;
}

static const IMAGE_NT_HEADERS *GetHeaders(const BYTE *data, size_t size)
{
    if (!data || size < sizeof(IMAGE_DOS_HEADER))
        return nullptr;
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
        return nullptr;
    const size_t offset = static_cast<size_t>(dos->e_lfanew);
    if (!Contains(size, offset, sizeof(IMAGE_NT_HEADERS)))
        return nullptr;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(data + offset);
#ifdef _WIN64
    constexpr WORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
    constexpr WORD machine = IMAGE_FILE_MACHINE_I386;
#endif
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != machine ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER))
        return nullptr;
    const size_t sectionOffset =
        offset + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    const size_t sectionSize =
        static_cast<size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (!Contains(size, sectionOffset, sectionSize) ||
        !Contains(nt->OptionalHeader.SizeOfHeaders, sectionOffset, sectionSize) ||
        nt->OptionalHeader.SizeOfHeaders > size ||
        nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage)
        return nullptr;
    return nt;
}

PE_IMAGE::~PE_IMAGE()
{
    ClosePE(this);
}

bool LoadPE(PE_HANDLE image, const uint8_t *data, size_t size)
{
    const auto *nt = GetHeaders(data, size);
    if (!nt)
        return FALSE;
    const auto *sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto &section = sections[i];
        if (!Contains(nt->OptionalHeader.SizeOfImage, section.VirtualAddress,
                      (std::max)(section.Misc.VirtualSize, section.SizeOfRawData)) ||
            (section.SizeOfRawData && !Contains(size, section.PointerToRawData, section.SizeOfRawData)))
            return FALSE;
    }
    auto *buffer = static_cast<BYTE *>(
        VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!buffer)
        return FALSE;
    std::memcpy(buffer, data, nt->OptionalHeader.SizeOfHeaders);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sections[i].SizeOfRawData)
            std::memcpy(buffer + sections[i].VirtualAddress, data + sections[i].PointerToRawData,
                        sections[i].SizeOfRawData);
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const UINT_PTR imageBase = static_cast<UINT_PTR>(nt->OptionalHeader.ImageBase);
    ClosePE(image);
    image->buffer = buffer;
    image->ImageSize = imageSize;
    image->ImageBase = imageBase;
    return TRUE;
}

uint8_t *SavePE(PE_HANDLE image, size_t *size)
{
    *size = 0;
    if (!image)
        return nullptr;
    const auto *nt = GetHeaders(image->buffer, image->ImageSize);
    if (!nt)
        return nullptr;
    const auto *sections = IMAGE_FIRST_SECTION(nt);
    size_t fileSize = nt->OptionalHeader.SizeOfHeaders;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto &section = sections[i];
        if (!Contains(image->ImageSize, section.VirtualAddress, section.SizeOfRawData) ||
            !Contains(MAXDWORD, section.PointerToRawData, section.SizeOfRawData))
            return nullptr;
        fileSize = (std::max)(fileSize, static_cast<size_t>(section.PointerToRawData) + section.SizeOfRawData);
    }
    auto *data = static_cast<BYTE *>(std::calloc(fileSize, 1));
    if (!data)
        return nullptr;
    std::memcpy(data, image->buffer, nt->OptionalHeader.SizeOfHeaders);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sections[i].SizeOfRawData)
            std::memcpy(data + sections[i].PointerToRawData, image->buffer + sections[i].VirtualAddress,
                        sections[i].SizeOfRawData);
    // The certificate overlay is not retained when rebuilding the executable.
    auto *outputNt = reinterpret_cast<IMAGE_NT_HEADERS *>(
        data + reinterpret_cast<const IMAGE_DOS_HEADER *>(data)->e_lfanew);
    outputNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY] = {};
    *size = fileSize;
    return data;
}

void ClosePE(PE_HANDLE image)
{
    if (image->buffer)
        VirtualFree(image->buffer, 0, MEM_RELEASE);
    image->buffer = nullptr;
    image->ImageSize = 0;
    image->ImageBase = 0;
}

bool LoadPEFile(PE_HANDLE image, const wchar_t* path)
{
	size_t fileSize;
	BYTE* fileData = (BYTE*)LoadFileData(path, &fileSize);
	if (!fileData) {
		return FALSE;
	}
	BOOL isOK = LoadPE(image, fileData, fileSize);
	std::free(fileData);
	return isOK;
}

bool SavePEFile(PE_HANDLE image, const wchar_t* path)
{
	size_t fileSize;
	BYTE* fileData = SavePE(image, &fileSize);
	if (!fileData) {
		return FALSE;
	}
	BOOL isOK = SaveFileData(path, fileData, fileSize);
	std::free(fileData);
	return isOK;
}

uintptr_t ToBufferAddress(PE_HANDLE hPE, uintptr_t Address)
{
	Address -= hPE->ImageBase;
	if (Address > hPE->ImageSize) {
		return NULL;
	}
	return Address + (uintptr_t)hPE->buffer;
}

uintptr_t ToVirtualAddress(PE_HANDLE hPE, uintptr_t Address)
{
	Address -= (uintptr_t)hPE->buffer;
	if (Address > hPE->ImageSize) {
		return NULL;
	}
	return Address + hPE->ImageBase;
}
