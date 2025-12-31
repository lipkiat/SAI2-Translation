#pragma once

struct PE_IMAGE
{
	BYTE* buffer;
	DWORD ImageSize;
	UINT_PTR ImageBase;
};
typedef struct PE_IMAGE PE_IMAGE, *PE_HANDLE;

BOOL LoadPE(PE_HANDLE hPE, BYTE* fileData, size_t fileSize);
BYTE* SavePE(PE_HANDLE hPE, size_t* size);
void ClosePE(PE_HANDLE hPE);

BOOL LoadPEFile(PE_HANDLE hPE, const wchar_t* fileName);
BOOL SavePEFile(PE_HANDLE hPE, const wchar_t* fileName);
