#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

#include "common.h"

// Files use UTF-8; Windows wchar_t and the executable's strings use UTF-16.
// Strict conversion prevents invalid input from silently becoming replacement characters.
bool DecodeUtf8(const std::string &bytes, std::wstring &text)
{
    text.clear();
    if (bytes.empty())
        return true;
    if (bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;
    const int size = static_cast<int>(bytes.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
    if (length == 0)
        return false;
    text.resize(length);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, &text[0], length) == length;
}

bool EncodeUtf8(const std::wstring &text, std::string &bytes)
{
    bytes.clear();
    if (text.empty())
        return true;
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;
    const int size = static_cast<int>(text.size());
    const int length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (length == 0)
        return false;
    bytes.resize(length);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, &bytes[0], length, nullptr, nullptr) == length;
}


void PrintMessage(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    const int length = _vscwprintf(format, args);
    va_end(args);
    if (length < 0)
        return;
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    va_start(args, format);
    vswprintf_s(buffer.data(), buffer.size(), format, args);
    va_end(args);
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(output, &mode))
    {
        WriteConsoleW(output, buffer.data(), static_cast<DWORD>(length), &written, nullptr);
    }
    else
    {
        const int bytes =
            WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0)
            return;
        std::vector<char> utf8(bytes);
        WideCharToMultiByte(CP_UTF8, 0, buffer.data(), length, utf8.data(), bytes, nullptr, nullptr);
        WriteFile(output, utf8.data(), static_cast<DWORD>(bytes), &written, nullptr);
    }
}

uint8_t* HexSearch(const uint8_t* bytes, size_t bytes_len, const char* wildcard)
{
	static char table[] = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x00\x00\x00\x00\x00\x00\x00\x0A\x0B\x0C\x0D\x0E\x0F\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0A\x0B\x0C\x0D\x0E\x0F\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

	size_t wildcard_len = 0; while (wildcard[wildcard_len]) wildcard_len++;
	size_t pattern_len = 0;
	uint8_t* pattern = (uint8_t*)malloc(wildcard_len/2);
	uint8_t* masks = (uint8_t*)malloc(wildcard_len/2);

	size_t i = 0;
	uint8_t decimal;
	uint8_t mask;
	while (i < wildcard_len)
	{
		if (wildcard[i] == ' ') { i++; continue; } // 跳过空格
		decimal = 0;
		mask = 0;
		if (wildcard[i] != '?')
		{
			mask = 0xF0;
			decimal = table[wildcard[i]] << 4;
		}
		i++;
		if (wildcard[i] != '?')
		{
			mask |= 0x0F;
			decimal |= table[wildcard[i]];
		}
		i++;
		pattern[pattern_len] = decimal;
		masks[pattern_len] = mask;
		pattern_len++;
	}

	uint8_t* result = NULL;
	for (const uint8_t* tail = bytes+(bytes_len-pattern_len); bytes <= tail; bytes++)
	{
		for (size_t i = 0; i < pattern_len; i++)
			if (((bytes[i]^pattern[i])&masks[i]) != 0) goto label;
		result = (uint8_t*)bytes;
		break;
		label:;
	}

	free(masks);
	free(pattern);
	return result;
}
