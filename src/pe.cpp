#include "pe.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
class FileHandle
{
  public:
    explicit FileHandle(HANDLE value) : value_(value)
    {
    }
    ~FileHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;
    HANDLE get() const
    {
        return value_;
    }

  private:
    HANDLE value_;
};

using ByteBuffer = std::unique_ptr<BYTE, decltype(&std::free)>;

bool Contains(size_t total, size_t offset, size_t length)
{
    return offset <= total && length <= total - offset;
}

const IMAGE_NT_HEADERS *GetHeaders(const BYTE *data, size_t size)
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

ByteBuffer ReadFileData(const wchar_t *path, size_t &size)
{
    size = 0;
    FileHandle file(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER length{};
    if (file.get() == INVALID_HANDLE_VALUE || !GetFileSizeEx(file.get(), &length) || length.QuadPart <= 0 ||
        static_cast<unsigned long long>(length.QuadPart) > (std::numeric_limits<size_t>::max)())
        return ByteBuffer(nullptr, &std::free);
    const size_t fileSize = static_cast<size_t>(length.QuadPart);
    ByteBuffer data(static_cast<BYTE *>(std::malloc(fileSize)), &std::free);
    if (!data)
        return data;
    size_t offset = 0;
    while (offset < fileSize)
    {
        const DWORD chunk = static_cast<DWORD>((std::min)(fileSize - offset, size_t{1} << 30));
        DWORD read = 0;
        if (!ReadFile(file.get(), data.get() + offset, chunk, &read, nullptr) || read == 0)
            return ByteBuffer(nullptr, &std::free);
        offset += read;
    }
    size = fileSize;
    return data;
}

bool WriteFileData(const wchar_t *path, const BYTE *data, size_t size)
{
    FileHandle file(
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        return false;
    size_t offset = 0;
    while (offset < size)
    {
        const DWORD chunk = static_cast<DWORD>((std::min)(size - offset, size_t{1} << 30));
        DWORD written = 0;
        if (!WriteFile(file.get(), data + offset, chunk, &written, nullptr) || written == 0)
            return false;
        offset += written;
    }
    return true;
}
} // namespace

PE_IMAGE::~PE_IMAGE()
{
    ClosePE(this);
}

BOOL LoadPE(PE_HANDLE image, const BYTE *data, size_t size)
{
    if (!image)
        return FALSE;
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

BYTE *SavePE(PE_HANDLE image, size_t *size)
{
    if (!size)
        return nullptr;
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
        fileSize =
            (std::max)(fileSize, static_cast<size_t>(section.PointerToRawData) + section.SizeOfRawData);
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
    if (!image)
        return;
    if (image->buffer)
        VirtualFree(image->buffer, 0, MEM_RELEASE);
    image->buffer = nullptr;
    image->ImageSize = 0;
    image->ImageBase = 0;
}

BOOL LoadPEFile(PE_HANDLE image, const wchar_t *path)
{
    if (!image || !path)
        return FALSE;
    size_t size = 0;
    auto data = ReadFileData(path, size);
    return data && LoadPE(image, data.get(), size);
}

BOOL SavePEFile(PE_HANDLE image, const wchar_t *path)
{
    if (!image || !path)
        return FALSE;
    size_t size = 0;
    ByteBuffer data(SavePE(image, &size), &std::free);
    return data && WriteFileData(path, data.get(), size);
}

UINT_PTR ToBufferAddress(PE_HANDLE hPE, UINT_PTR Address)
{
	Address -= hPE->ImageBase;
	if (Address > hPE->ImageSize)
	{
		return NULL;
	}
	return Address + (UINT_PTR)hPE->buffer;
}

UINT_PTR ToVirtualAddress(PE_HANDLE hPE, UINT_PTR Address)
{
	Address -= (UINT_PTR)hPE->buffer;
	if (Address > hPE->ImageSize)
	{
		return NULL;
	}
	return Address + hPE->ImageBase;
}
