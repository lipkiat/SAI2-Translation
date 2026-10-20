#pragma once
#include <cstdint>
#include <cstring>
#include <string>

bool DecodeUtf8(const std::string &bytes, std::wstring &text);
bool EncodeUtf8(const std::wstring &text, std::string &bytes);

void PrintMessage(const wchar_t *format, ...);

uint8_t* HexSearch(const uint8_t* bytes, size_t bytes_len, const char* wildcard);
