#pragma once
#include <cstdint>

void PrintMessage(const wchar_t *format, ...);

uint8_t* HexSearch(const uint8_t* bytes, size_t bytes_len, const char* wildcard);
