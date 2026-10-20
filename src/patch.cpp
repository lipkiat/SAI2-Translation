#include <cstdint>
#include <cstring>
#include <vector>
#include "common.h"
#include "patch.h"

static bool Contains(PE_HANDLE image, uintptr_t address, size_t size)
{
    const auto base = reinterpret_cast<uintptr_t>(image->buffer);
    return address >= base && address - base <= image->ImageSize &&
           size <= image->ImageSize - (address - base);
}

static uintptr_t ReadRelativeAddress32(const void *address)
{
    return reinterpret_cast<uintptr_t>(address) + sizeof(int32_t) + *reinterpret_cast<const int32_t *>(address);
}

static void WriteRelativeAddress32(void *address, uintptr_t value)
{
    *reinterpret_cast<int32_t *>(address) = static_cast<int32_t>(value - reinterpret_cast<uintptr_t>(address) - sizeof(int32_t));
}

bool ApplyBinaryPatch(PE_HANDLE image)
{
    if (!image || !image->buffer)
        return false;
    uint8_t *p = HexSearch(image->buffer, image->ImageSize,
        "8B 05 ???????? 39 44 24 44 0F 84");
    if (!p)
        return false;
    uintptr_t idAddress = ReadRelativeAddress32(p + 2);
    if (!Contains(image, idAddress, 4))
        return false;
    PrintMessage(L"[crack] id_address %p\n", reinterpret_cast<void *>(ToVirtualAddress(image, idAddress)));
    uintptr_t call;
    p = HexSearch(image->buffer, image->ImageSize,
        "48 89 44 24 38 48 8D 05 ???????? 4C 8D 44 24 40 48 8D 4C 24 30 48 89 44 24 30 E8 ???????? 48 8D 4C 24 40 E8 ???????? 85 C0");
    if (p != nullptr)
    {
        // 新版
        p += 0x1B;
        call = ReadRelativeAddress32(p + 1);
        p += 0xA;
        uintptr_t call2 = ReadRelativeAddress32(p + 1);
        PrintMessage(L"[crack] call2_address %p\n", reinterpret_cast<void *>(ToVirtualAddress(image, call2)));
        if (!Contains(image, call2, 5))
            return false;
        std::memcpy(reinterpret_cast<void *>(call2), "\x48\x31\xC0\xC3\x90", 5); // xor rax,rax | ret
    }
    else
    {
        // 旧版
        p = HexSearch(image->buffer, image->ImageSize,
            "E8 ???????? 83 7C 24 40 01 0F 85 ????0000 83 7C 24 54 01 0F 85 ????0000 83 7C 24 5C 01 0F 85 ????0000 8B 05 ???????? 39 44 24 44");
        if (p == nullptr)
            return false;
        call = ReadRelativeAddress32(p + 1);
    }
    PrintMessage(L"[crack] call_address %p\n", reinterpret_cast<void *>(ToVirtualAddress(image, call)));
    if (!Contains(image, call, 0x23))
        return false;
    std::memcpy(reinterpret_cast<void *>(call),
        "\x41\xC7\x00\x01\x00\x00\x00\x41\xC7\x40\x14\x01\x00\x00\x00\x41\xC7\x40\x1C\x01\x00\x00\x00\x8B\x05\x00\x00\x00\x00\x41\x89\x40\x04\xC3\x90",
        0x23);
    call += 0x19;
    WriteRelativeAddress32(reinterpret_cast<void *>(call), idAddress);

    // 免创建 .slc 文件
    p = image->buffer;
    size_t len = image->ImageSize;
    while (len)
    {
        // L".slc"
        uint8_t *p2 = HexSearch(p, len, "2E 00 73 00 6C 00 63 00 00 00");
        if (p2 == nullptr)
            break;
        len -= p2 - p;
        std::memcpy(p2, L".ini", 10);
        p = p2 + 10;
        len -= 10;
    }

    PrintMessage(L"[crack] applied patch.\n");
    return true;
}
