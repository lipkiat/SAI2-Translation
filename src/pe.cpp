#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#include <memory.h>

#include "pe.h"

static void* LoadFileData(const wchar_t* path, size_t* size)
{
	void* data = NULL;
	LARGE_INTEGER fileSize;
	DWORD bytesRead;
	size_t remainingSize;
	char* ptr;
	const size_t chunkSize = 2147483648; // 2GB
	DWORD currentChunkSize;
	// 打开文件
	HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}
	do {
		// 获取文件大小
		if (!GetFileSizeEx(hFile, &fileSize)) {
			break;
		}
		remainingSize = (size_t)fileSize.QuadPart;
		*size = remainingSize;
		// 分配内存
		data = malloc(remainingSize);
		if (!data) {
			break;
		}
		// 分块读取文件，支持大于 4GB 的文件
		ptr = (char*)data;
		while (remainingSize > 0) {
			currentChunkSize = (DWORD)min(chunkSize, remainingSize);
			if (!ReadFile(hFile, ptr, currentChunkSize, &bytesRead, NULL)) {
				free(data);
				data = NULL;
				break;
			}
			ptr += bytesRead;
			remainingSize -= bytesRead;
		}
	} while(false);
	// 关闭文件
	CloseHandle(hFile);
	return data;
}

static BOOL SaveFileData(const wchar_t* path, void* data, size_t size)
{
	DWORD bytesWritten;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	BOOL isOK = WriteFile(h, data, (DWORD)size, &bytesWritten, NULL);
	CloseHandle(h);
	return isOK;
}



BOOL LoadPE(PE_HANDLE hPE, BYTE* fileData, size_t fileSize)
{
	PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)fileData;
	if (IMAGE_DOS_SIGNATURE != dos->e_magic) return FALSE;
	PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)fileData + dos->e_lfanew);
	if (IMAGE_NT_SIGNATURE != nt->Signature) return FALSE;
#ifdef _WIN64
	if (IMAGE_FILE_MACHINE_AMD64 != nt->FileHeader.Machine) return FALSE;
#else
	if (IMAGE_FILE_MACHINE_I386 != nt->FileHeader.Machine) return FALSE;
#endif

	hPE->ImageSize = nt->OptionalHeader.SizeOfImage;
	hPE->buffer = (BYTE*)VirtualAlloc(NULL, hPE->ImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (hPE->buffer == NULL)
	{
		return FALSE;
	}
	//memset(hPE->buffer, 0, hPE->ImageSize);

	// Header
	memcpy(hPE->buffer, fileData, nt->OptionalHeader.SizeOfHeaders);

	// Section
	IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)((UINT_PTR)nt + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);
	WORD sectionNumber = nt->FileHeader.NumberOfSections;
	for (WORD i = 0; i < sectionNumber; i++)
	{
		memcpy(hPE->buffer + sections[i].VirtualAddress, fileData + sections[i].PointerToRawData, sections[i].SizeOfRawData);
	}

	hPE->ImageBase = nt->OptionalHeader.ImageBase;
	return TRUE;
}

BYTE* SavePE(PE_HANDLE hPE, size_t* size)
{
	PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hPE->buffer;
	PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hPE->buffer + dos->e_lfanew);
	dos->e_magic = IMAGE_DOS_SIGNATURE;
	nt->Signature = IMAGE_NT_SIGNATURE;
	PIMAGE_DATA_DIRECTORY pDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
	pDir->VirtualAddress = 0;
	pDir->Size = 0;

	// 计算文件大小
	DWORD fileSize = nt->OptionalHeader.SizeOfHeaders;
	WORD nSections = nt->FileHeader.NumberOfSections;
	IMAGE_SECTION_HEADER *pSec = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nSections; ++i)
	{
		DWORD end = pSec[i].PointerToRawData + pSec[i].SizeOfRawData;
		if (end > fileSize) fileSize = end;
	}
	*size = fileSize;

	BYTE* fileBuffer = (BYTE*)malloc(fileSize);
	if(fileBuffer)
	{
		memset(fileBuffer, 0, fileSize);

		// Copy the file header
		memcpy(fileBuffer, hPE->buffer, nt->OptionalHeader.SizeOfHeaders);

		// Copy each section back
		for (WORD i = 0; i < nSections; ++i, ++pSec)
		{
			memcpy(fileBuffer + pSec->PointerToRawData, hPE->buffer + pSec->VirtualAddress, pSec->SizeOfRawData);
		}
	}
	return fileBuffer;
}

void ClosePE(PE_HANDLE hPE)
{
	VirtualFree(hPE->buffer, 0, MEM_RELEASE);
	hPE->buffer = NULL;
}

BOOL LoadPEFile(PE_HANDLE hPE, const wchar_t* fileName)
{
	size_t fileSize;
	BYTE* fileData = (BYTE*)LoadFileData(fileName, &fileSize);
	if (!fileData)
	{
		return FALSE;
	}
	BOOL isOK = LoadPE(hPE, fileData, fileSize);
	free(fileData);
	return isOK;
}

BOOL SavePEFile(PE_HANDLE hPE, const wchar_t* fileName)
{
	size_t fileSize;
	BYTE* fileData = SavePE(hPE, &fileSize);
	if (!fileData)
	{
		return FALSE;
	}
	BOOL isOK = SaveFileData(fileName, fileData, fileSize);
	free(fileData);
	return isOK;
}
