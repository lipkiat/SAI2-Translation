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

void my_printf(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	int length = wvsprintfA(buffer, format, ap);
	va_end(ap);
	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, NULL, NULL);
}

void my_wprintf(const wchar_t* format, ...)
{
	va_list ap;
	va_start(ap, format);
	wchar_t buffer[1024];
	memset(buffer, 0, sizeof(buffer));
	int length = wvsprintfW(buffer, format, ap);
	va_end(ap);
	WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, NULL, NULL);
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
};

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

struct SAI_LocalLanguage
{
	UINT_PTR name;
	UINT_PTR text;
	size_t id;
};

void printLang(PE_HANDLE hPE, UINT_PTR Address)
{
	SAI_LocalLanguage* langs = (SAI_LocalLanguage*)ToBufferAddress(hPE, Address);
	do {
		wchar_t* name = (wchar_t*)ToBufferAddress(hPE, langs->name);
		if (!name) break;
		wchar_t* text = (wchar_t*)ToBufferAddress(hPE, langs->text);
		if (!text) break;
		size_t id = langs->id;
		//my_wprintf(L"%X ", id);
		text = escape_special_wchars(text);
		my_wprintf(L"%ls=%ls\n", name, text);
		free(text);
		langs++;
	} while (true);
}

struct QueueInfo
{
	SAI_LocalLanguage* lang;
	wchar_t* value;
};
std::vector<QueueInfo> queues;
MemoryFragmentPool pool;


int loadLangFile(PE_HANDLE hPE, const std::wstring filename, UINT_PTR Address)
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
	std::map<std::wstring, wchar_t*> cfg;
	std::wstring line;
	while (std::getline(fin, line))
	{
		// 处理 Windows 换行符：getline 只会把 '\n' 去掉，残留 '\r' 需要手动去除
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
	// 结果演示
	my_wprintf(L"读取完成，共 %d 条记录。\n", cfg.size());
	/*
	for (const auto& kv : cfg)
	{
		my_wprintf(L"%ls=%ls\n", kv.first.c_str(), kv.second.c_str());
	}
	*/

	// 修改
	{
		SAI_LocalLanguage* langs = (SAI_LocalLanguage*)ToBufferAddress(hPE, Address);
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
	my_wprintf(L"\n");
	return 0;
}

void doQueue(PE_HANDLE hPE)
{
	for (const auto& q : queues)
	{
		SAI_LocalLanguage* lang = q.lang;
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

int wmain(int argc, wchar_t *argv[])
{
	/*
	for(int i = 0; i < argc; i++)
	{
		my_wprintf(L"%ls\n", argv[i]);
	}
	*/
	wchar_t *OriginalFile;
	wchar_t *Dir;
	wchar_t FilePath[MAX_PATH];
	if (argc > 1)
	{
		OriginalFile = argv[1];
	}
	if (argc > 2)
	{
		Dir = argv[2];
		my_wprintf(L"原始文件：%ls\n", OriginalFile);
		my_wprintf(L"目标目录：%ls\n", Dir);
	}

	PE_IMAGE image;
	if (!LoadPEFile(&image, OriginalFile))
	{
		my_printf("LoadPEFile 失败\n");
		return 1;
	}

	// 注意：内存地址为硬编码，对应 SAI2 20241123（64位） 版本。
	// 后续有必要时我们将尝试自动化的特征码定位。

	if (!Dir)
	{
		Dir = L".";
		my_printf("# Main.txt\n"); printLang(&image, 0x140266C80);
		my_printf("# MainTip.txt\n"); printLang(&image, 0x14026E020);
		my_printf("# Import.txt\n"); printLang(&image, 0x140284A10);
		my_printf("# Option.txt\n"); printLang(&image, 0x1402869C0);
		my_printf("# Errdlg.txt\n"); printLang(&image, 0x14028FEB0);
	}
	else
	{
		wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\lang\\Main.txt");
		loadLangFile(&image, FilePath, 0x14025E770);
		wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\lang\\MainTip.txt");
		loadLangFile(&image, FilePath, 0x140265B10);
		wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\lang\\Import.txt");
		loadLangFile(&image, FilePath, 0x140284840);
		wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\lang\\Option.txt");
		loadLangFile(&image, FilePath, 0x140285FA0);
		wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\lang\\Errdlg.txt");
		loadLangFile(&image, FilePath, 0x14028FB30);
		doQueue(&image);
	}


	wcscpy_s(FilePath, MAX_PATH, Dir); wcscat_s(FilePath, MAX_PATH, L"\\sai2.exe");
	if (!SavePEFile(&image, FilePath))
	{
		my_printf("SavePEFile 失败\n");
		return 1;
	}
	ClosePE(&image);
	return 0;

	my_printf("\nOK.\n");
	return 0;
}
