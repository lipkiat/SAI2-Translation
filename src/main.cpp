// https://github.com/lipkiat/SAI2-Translation
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <fstream>
#include <iostream>
#include <locale>
#include <codecvt>
#include <map>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <vector>

#include "pe.h"

struct SAI_LocalizationEntry
{
	UINT_PTR name; // key
	UINT_PTR text; // value
	size_t id;
};

enum class SAI_LocalizationDomain
{
    Main, MainTip, Import, Option, Errdlg
};

enum class SAI_LocalizationLanguage
{
    Japanese, English
};


struct QueueInfo
{
	SAI_LocalizationEntry* lang;
	wchar_t* value;
};

std::vector<QueueInfo> queues;

void my_printf(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	char buffer[2048];
	memset(buffer, 0, sizeof(buffer));
	int length = wvsprintfA(buffer, format, ap);
	va_end(ap);
	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, NULL, NULL);
}

void my_wprintf(const wchar_t* format, ...)
{
	va_list ap;
	va_start(ap, format);
	wchar_t buffer[2048];
	memset(buffer, 0, sizeof(buffer));
	int length = wvsprintfW(buffer, format, ap);
	va_end(ap);
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, NULL, NULL);
}

void my_fprintf(HANDLE hFile, const wchar_t* format, ...)
{
	va_list ap;
	va_start(ap, format);
	wchar_t buffer[2048];
	memset(buffer, 0, sizeof(buffer));
	int length = wvsprintfW(buffer, format, ap);
	va_end(ap);
	DWORD bytesWritten;
	length = length * 2;
    WriteFile(hFile, buffer, length, &bytesWritten, NULL);
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

wchar_t *escape_special_wchars(const wchar_t *src)
{
	size_t src_len = wcslen(src);
	size_t dst_len = src_len * 2;
	wchar_t *dst = (wchar_t *)malloc((dst_len + 1) * sizeof(wchar_t));
	if (!dst) return NULL;
	size_t j = 0;
	for (size_t i = 0; i < src_len; ++i) {
		if (src[i] == L'\\') {
			dst[j++] = L'\\';
			dst[j++] = L'\\';
		} else if (src[i] == L'\t') {
			dst[j++] = L'\\';
			dst[j++] = L't';
		} else if (src[i] == L'\r') {
			dst[j++] = L'\\';
			dst[j++] = L'r';
		} else if (src[i] == L'\n') {
			dst[j++] = L'\\';
			dst[j++] = L'n';
		} else {
			dst[j++] = src[i];
		}
	}
	dst[j] = L'\0';
	return dst;
}

wchar_t *unescape_special_wchars(const wchar_t *src)
{
	size_t src_len = wcslen(src);
	wchar_t *dst = (wchar_t *)malloc((src_len + 1) * sizeof(wchar_t));
	if (!dst) return NULL;
	size_t j = 0;
	for (size_t i = 0; i < src_len; ++i) {
		if (src[i] == L'\\' && i + 1 < src_len) {
			switch (src[i + 1]) {
				case L'\\':  dst[j++] = L'\\'; i++; break;
				case L't':   dst[j++] = L'\t'; i++; break;
				case L'r':   dst[j++] = L'\r'; i++; break;
				case L'n':   dst[j++] = L'\n'; i++; break;
				default:     dst[j++] = src[i]; //未知转义，保留'\'
			}
		} else {
			dst[j++] = src[i];
		}
	}
	dst[j] = L'\0';
	return dst;
}

class MemoryFragmentPool
{
public:
	/** 回收：addr 为起始地址，size 为块大小 */
	void free(uintptr_t addr, size_t size)
	{
		free_blocks_.emplace(size, addr);
		// 若需要调试，打印回收信息
		//std::cout << "[free] addr=" << std::setw(4) << addr << ", size=" << size << '\n';
	}
	/** 分配：返回起始地址；如果没有合适的空闲块，则返回0 */
	uintptr_t allocate(size_t size)
	{
		// 在 free_blocks_ 中找第一个 >= size 的块
		auto it = free_blocks_.lower_bound(size);
		if (it != free_blocks_.end())
		{
			uintptr_t addr = it->second;
			size_t block_sz = it->first;
			free_blocks_.erase(it);
			// 记录分配信息
			//std::cout << "[alloc] using free block, addr=" << std::setw(4) << addr << ", size=" << size << '\n';
			// 若块更大，拆分剩余部分
			if (block_sz > size)
			{
				uintptr_t leftover_addr = addr + size;
				size_t leftover_size   = block_sz - size;
				free_blocks_.emplace(leftover_size, leftover_addr);
				//std::cout << "  -> leftover block: addr=" << std::setw(4) << leftover_addr << ", size=" << leftover_size << '\n';
			}
			return addr;
		}
		else
		{
			// 没有合适的空闲块
			return 0;
		}
	}
	/** 打印当前空闲块（按大小升序） */
	void print() const
	{
		std::cout << "\n=== 当前空闲块 ===\n";
		for (const auto &p : free_blocks_)
		{
			std::cout << "  size=" << std::setw(3) << p.first
					  << ", addr=" << std::setw(4) << p.second << '\n';
		}
		std::cout << "=================\n";
	}
private:
	std::multimap<size_t, uintptr_t> free_blocks_;
} pool;


void printLang(PE_HANDLE hPE, UINT_PTR Address, wchar_t* file_name)
{
	HANDLE hFile = CreateFile(file_name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
        my_wprintf(L"无法创建文件 (%d).\n", GetLastError());
        return;
    }
	BYTE bom[] = {0xFF, 0xFE};
	DWORD bytesWritten;
	WriteFile(hFile, bom, sizeof(bom), &bytesWritten, NULL);

	SAI_LocalizationEntry* langs = (SAI_LocalizationEntry*)ToBufferAddress(hPE, Address);
	do {
		wchar_t* name = (wchar_t*)ToBufferAddress(hPE, langs->name);
		if (!name) break;
		wchar_t* text = (wchar_t*)ToBufferAddress(hPE, langs->text);
		if (!text) break;
		size_t id = langs->id;
		//my_wprintf(L"%X ", id);
		text = escape_special_wchars(text);
		my_fprintf(hFile, L"%ls=%ls\n", name, text);
		free(text);
		langs++;
	} while (true);

	CloseHandle(hFile);
}

void changeLanguage(PE_HANDLE hPE, UINT_PTR Address, std::map<std::wstring, wchar_t*> &cfg)
{
	// 修改
	{
		SAI_LocalizationEntry* langs = (SAI_LocalizationEntry*)ToBufferAddress(hPE, Address);
		do {
			wchar_t* key = (wchar_t*)ToBufferAddress(hPE, langs->name);
			if (!key) break;
			wchar_t* text = (wchar_t*)ToBufferAddress(hPE, langs->text);
			if (!text) break;
			size_t id = langs->id;
			//my_wprintf(L"%X ", id);
			//my_wprintf(L"%ls=%ls\n", name, text);
			auto it = cfg.find(key);
			if (it == cfg.end())
			{
				my_wprintf(L"找不到Key:%ls\n", key);
			}
			else
			{
				wchar_t* newtext = it->second;
				int src_len = (int)wcslen(text);
				int new_len = (int)wcslen(newtext);
				if (new_len > src_len)
				{
					//my_wprintf(L"%ls 太长(%d>%d):%ls\n", key, new_len, src_len, newtext);
					//my_wprintf(L"%ls\n", text);
					queues.push_back({langs, newtext});
					//memset(text, 0, src_len);
					//pool.free((uintptr_t)text, src_len);
				}
				else
				{
					lstrcpyW(text, newtext);
					free(newtext);
					text += 1 + new_len;
					src_len -= 1;
					src_len -= new_len;
					if (src_len > 1)
					{
						if (*text != 0)
						{
							memset(text, 0, src_len*2);
							pool.free((uintptr_t)text, src_len*2);
						}
						else
						{
							my_wprintf(L"重复释放 %p\n", text);
						}
					}
				}
			}

			langs++;
		} while (true);
	}
}

wchar_t *target_language;

void getLangFilePath(wchar_t* buffer, SAI_LocalizationDomain domain, wchar_t* language)
{
	wchar_t* file_name = NULL;
	wcscpy_s(buffer, MAX_PATH, language);
	switch(domain)
	{
		case SAI_LocalizationDomain::Main:
			file_name = L"\\lang\\Main.txt";
			break;
		case SAI_LocalizationDomain::MainTip:
			file_name = L"\\lang\\MainTip.txt";
			break;
		case SAI_LocalizationDomain::Import:
			file_name = L"\\lang\\Import.txt";
			break;
		case SAI_LocalizationDomain::Option:
			file_name = L"\\lang\\Option.txt";
			break;
		case SAI_LocalizationDomain::Errdlg:
			file_name = L"\\lang\\Errdlg.txt";
			break;
	}
	wcscat_s(buffer, MAX_PATH, file_name);
}

int loadLangFile(const std::wstring filename, std::map<std::wstring, wchar_t*> &cfg)
{
	// 打开文件
	std::wifstream fin(filename, std::ios::binary);
	if (!fin)
	{
		my_wprintf(L"无法打开文件\n");
		return 1;
	}
	// 设定 UTF‑16LE 的 locale
	fin.imbue(std::locale(fin.getloc(), new std::codecvt_utf16<wchar_t, 0x10ffff, std::consume_header>));

	// 读取并解析
	std::wstring line;
	cfg.clear();
	while (std::getline(fin, line))
	{
		// 处理 Windows 换行符：getline 只会把 '\n' 去掉，残留 '\r' 需要去除
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		// 跳过空行和注释行（假设以 # 开头）
		if (line.empty() || line[0] == L'#')
			continue;
		// 按等号分割
		auto pos = line.find(L'=');
		if (pos == std::wstring::npos)
		{
			my_wprintf(L"格式错误 (未找到 '='): %ls\n", line.c_str());
			continue;
		}
		std::wstring key = line.substr(0, pos);
		wchar_t* value = unescape_special_wchars(line.substr(pos + 1).c_str());
		cfg[key] = value; // 插入/覆盖
	}
	// 结果
	my_wprintf(L"读取完成，共 %d 条记录。\n", cfg.size());
	/*
	for (const auto& kv : cfg)
	{
		my_wprintf(L"%ls=%ls\n", kv.first.c_str(), kv.second.c_str());
	}
	*/
	return 0;
}

void doQueue(PE_HANDLE hPE)
{
	for (const auto& q : queues)
	{
		SAI_LocalizationEntry* lang = q.lang;
		wchar_t* key = (wchar_t*)ToBufferAddress(hPE, lang->name);
		wchar_t* text = (wchar_t*)ToBufferAddress(hPE, lang->text);
		size_t id = lang->id;
		wchar_t* newtext = q.value;
		int new_len = (int)wcslen(newtext);
		//my_wprintf(L"[Queue]%ls=%ls\n", key, newtext);
		uintptr_t new_addr = pool.allocate((new_len+1)*2);
		if (new_addr == 0)
		{
			my_wprintf(L"没有足够的空间容纳 %ls\n", newtext);
			continue;
		}
		lang->text = ToVirtualAddress(hPE, new_addr);
		lstrcpyW((wchar_t*)new_addr, newtext);
	}
}


int is_english_string(const wchar_t *str) {
    int en_count = 0; // 英文字符计数
    int other_count = 0; // 非英文字符计数
    while (*str != '\0') {
        wchar_t c = *str;
        str++;
        // 忽略不可见字符
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
		// 忽略数字
        if (c >= '0' && c <= '9') {
            continue;
        }

        // 是否为半角英文字母
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            en_count++;
        } else {
            other_count++;
        }
    }
    return en_count > other_count ? 1 : 0;
}

UINT_PTR scanLanguage(PE_HANDLE hPE, std::map<std::wstring, wchar_t*> &cfg, SAI_LocalizationLanguage language)
{
	my_wprintf(L"正在尝试特征定位...\n");
	BYTE* pEnd = hPE->buffer + hPE->ImageSize;
	pEnd -= sizeof(SAI_LocalizationEntry);
	// 为日文
	int best_success_count = 0;
	int best_other_count = 0;
	SAI_LocalizationEntry* best_entry = NULL;
	// 为英文
	int best_success_count2 = 0;
	int best_en_count = 0;
	SAI_LocalizationEntry* best_entry2 = NULL;

	for (BYTE* p = hPE->buffer; p < pEnd; p += sizeof(size_t))
	{
		int en_count = 0;
		int other_count = 0;
		int success_count = 0;
		int failure_count = 0;
		SAI_LocalizationEntry* entry = (SAI_LocalizationEntry*)p;
		while((BYTE*)entry < pEnd)
		{
			if (entry->id == 0) break;
			if (!(entry->name > hPE->ImageBase && entry->name < hPE->ImageBase + hPE->ImageSize))
				break;
			if (!(entry->text > hPE->ImageBase && entry->text < hPE->ImageBase + hPE->ImageSize))
				break;
			wchar_t* name = (wchar_t*)ToBufferAddress(hPE, entry->name);
			if (!name) break;
			wchar_t* text = (wchar_t*)ToBufferAddress(hPE, entry->text);
			if (!text) break;
			if (!is_english_string(name)) break;
			//my_wprintf(L"%ls\n", name);
			//my_wprintf(L"%ls\n", text);
			if (is_english_string(text))
			{
				en_count++;
			}
			else
			{
				other_count++;
			}
			if (cfg[name])
			{
				success_count++;
			}
			else
			{
				failure_count++;
			}
			entry++;
		}
		if (success_count != 0 && success_count > failure_count)
		{
			if (other_count > en_count && success_count > best_success_count && other_count > best_other_count)
			{
				best_success_count = success_count;
				best_other_count = other_count;
				best_entry = (SAI_LocalizationEntry*)p;
				my_wprintf(L"找到日文地址 %p\n", (void*)ToVirtualAddress(hPE, (UINT_PTR)p));
				my_wprintf(L"  成功数量 %d\n", success_count);
				my_wprintf(L"  失败数量 %d\n", failure_count);
				my_wprintf(L"  英文数量 %d\n", en_count);
				my_wprintf(L"  其它数量 %d\n", other_count);
			}
			if (en_count > other_count && success_count > best_success_count2 && en_count > best_en_count)
			{
				best_success_count2 = success_count;
				best_en_count = en_count;
				best_entry2 = (SAI_LocalizationEntry*)p;
				my_wprintf(L"找到英文地址 %p\n", (void*)ToVirtualAddress(hPE, (UINT_PTR)p));
				my_wprintf(L"  成功数量 %d\n", success_count);
				my_wprintf(L"  失败数量 %d\n", failure_count);
				my_wprintf(L"  英文数量 %d\n", en_count);
				my_wprintf(L"  其它数量 %d\n", other_count);
			}
		}
	}
	my_wprintf(L"最佳日文地址 %p\n", (void*)ToVirtualAddress(hPE, (UINT_PTR)best_entry));
	my_wprintf(L"最佳英文地址 %p\n", (void*)ToVirtualAddress(hPE, (UINT_PTR)best_entry2));
	switch (language)
	{
		case SAI_LocalizationLanguage::Japanese:
			return (UINT_PTR)ToVirtualAddress(hPE, (UINT_PTR)best_entry);
		case SAI_LocalizationLanguage::English:
			return (UINT_PTR)ToVirtualAddress(hPE, (UINT_PTR)best_entry2);
	}
	return 0;
}

void doTranslation(PE_HANDLE hPE, SAI_LocalizationDomain domain)
{
	wchar_t FilePath[MAX_PATH];
	std::map<std::wstring, wchar_t*> cfg;
	my_wprintf(L"\n");
	// 日语（原始语言）
	getLangFilePath(FilePath, domain, L"ja");
	my_wprintf(L"原始语言文件：%ls\n", FilePath);
	loadLangFile(FilePath, cfg);
	UINT_PTR Address = scanLanguage(hPE, cfg, SAI_LocalizationLanguage::Japanese);
	if (target_language)
	{
		// 目标语言
		getLangFilePath(FilePath, domain, target_language);
		my_wprintf(L"目标语言文件：%ls\n", FilePath);
		loadLangFile(FilePath, cfg);
		changeLanguage(hPE, Address, cfg);
	}
	else
	{
		// 进入打印模式
		getLangFilePath(FilePath, domain, L".");
		my_wprintf(L"文件：%ls\n", FilePath);
		printLang(hPE, Address, FilePath);
	}
}


uint8_t* hex_search(const uint8_t* bytes, size_t bytes_len, const char* wildcard)
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

// 相对寻址
_inline UINT_PTR RelativeAddressing8(void* a)
{
	return (INT_PTR)*(INT8*)a + (UINT_PTR)a + sizeof(INT8);
}
_inline UINT_PTR RelativeAddressing32(void* a)
{
	return (INT_PTR)*(INT32*)a + (UINT_PTR)a + sizeof(INT32);
}

void crack(PE_HANDLE hPE)
{
	uint8_t* p = hex_search(hPE->buffer, hPE->ImageSize, "8B 05 ???????? 39 44 24 44 0F 84");
	UINT_PTR id_address = RelativeAddressing32(p+2);
	my_printf("id_address %p\n", ToVirtualAddress(hPE, id_address));
	UINT_PTR call;
	p = hex_search(hPE->buffer, hPE->ImageSize, "48 89 44 24 38 48 8D 05 ???????? 4C 8D 44 24 40 48 8D 4C 24 30 48 89 44 24 30 E8 ???????? 48 8D 4C 24 40 E8 ???????? 85 C0");
	if (p != NULL) {
		// 新版
		p += 0x1B;
		call = RelativeAddressing32(p+1);
		p += 0xA;
		UINT_PTR call2 = RelativeAddressing32(p+1);
		my_printf("call2 %p\n", ToVirtualAddress(hPE, call2));
		memcpy((void*)call2, "\x48\x31\xC0\xC3\x90", 5); // xor rax,rax | ret
	} else {
		// 旧版
		p = hex_search(hPE->buffer, hPE->ImageSize, "E8 ???????? 83 7C 24 40 01 0F 85 ????0000 83 7C 24 54 01 0F 85 ????0000 83 7C 24 5C 01 0F 85 ????0000 8B 05 ???????? 39 44 24 44");
		if (p == NULL)
		{
			my_printf("无法定位特征码\n");
			return;
		}
		call = RelativeAddressing32(p+1);
	}
	my_printf("call %p\n", ToVirtualAddress(hPE, call));
	memcpy((void*)call, "\x41\xC7\x00\x01\x00\x00\x00\x41\xC7\x40\x14\x01\x00\x00\x00\x41\xC7\x40\x1C\x01\x00\x00\x00\x8B\x05\x00\x00\x00\x00\x41\x89\x40\x04\xC3\x90", 0x23);
	call += 0x19;
	*(INT32*)call = (INT32)((INT_PTR)id_address - (UINT_PTR)call - sizeof(INT32)); // 计算相对寻址

	p = hPE->buffer;
	size_t len = hPE->ImageSize;
	while (len)
	{
		uint8_t* p2;
		// L".slc"
		p2 = hex_search(p, len, "2E 00 73 00 6C 00 63 00 00 00");
		if (p2 == NULL)
		{
			break;
		}
		len -= p2-p;
		memcpy(p2, L".ini", 10);
		p = p2 + 10;
		len -= 10;
	}
	my_printf("已写入破解补丁！\n");
}

int wmain(int argc, wchar_t *argv[])
{
	wchar_t FilePath[MAX_PATH];
	wchar_t *OriginalFile = NULL;
	if (argc > 1)
	{
		OriginalFile = argv[1];
	}
	if (argc > 2)
	{
		target_language = argv[2];
		my_wprintf(L"原始文件：%ls\n", OriginalFile);
		my_wprintf(L"目标语言：%ls\n", target_language);
	}

	// 加载PE文件
	PE_IMAGE image;
	if (!LoadPEFile(&image, OriginalFile))
	{
		my_printf("LoadPEFile 失败\n");
		return 1;
	}

	// 破解
	crack(&image);
	//getchar();

	// 加载语言并翻译
	doTranslation(&image, SAI_LocalizationDomain::Main);
	doTranslation(&image, SAI_LocalizationDomain::MainTip);
	doTranslation(&image, SAI_LocalizationDomain::Import);
	doTranslation(&image, SAI_LocalizationDomain::Option);
	doTranslation(&image, SAI_LocalizationDomain::Errdlg);
	if (!target_language)
	{
		return 0;
	}
	doQueue(&image);
	// 保存到文件
	wcscpy_s(FilePath, MAX_PATH, target_language); wcscat_s(FilePath, MAX_PATH, L"\\sai2.exe");
	if (!SavePEFile(&image, FilePath))
	{
		my_printf("SavePEFile 失败\n");
		return 1;
	}
	
	ClosePE(&image);
	my_printf("\nOK.\n");
	return 0;
}
