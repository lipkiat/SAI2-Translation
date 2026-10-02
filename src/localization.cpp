#include "localization.h"
#include "common.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
struct LocalizationEntry
{
    UINT_PTR name;
    UINT_PTR text;
    size_t id;
};

using Translations = std::map<std::wstring, std::wstring>;

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

struct PendingTranslation
{
    LocalizationEntry *entry;
    std::wstring value;
};

class MemoryFragmentPool
{
public:
    void Release(UINT_PTR address, size_t size)
    {
        blocks_.emplace(size, address);
    }
    UINT_PTR Allocate(size_t size)
    {
        const auto it = blocks_.lower_bound(size);
        if (it == blocks_.end())
            return 0;
        const auto address = it->second;
        const auto remaining = it->first - size;
        blocks_.erase(it);
        if (remaining)
            blocks_.emplace(remaining, address + size);
        return address;
    }

private:
    std::multimap<size_t, UINT_PTR> blocks_;
};

bool Contains(PE_HANDLE image, const void *pointer, size_t size)
{
    const auto address = reinterpret_cast<UINT_PTR>(pointer);
    const auto base = reinterpret_cast<UINT_PTR>(image->buffer);
    return address >= base && address - base <= image->ImageSize &&
           size <= image->ImageSize - (address - base);
}

wchar_t *GetString(PE_HANDLE image, UINT_PTR address)
{
    auto *value = reinterpret_cast<wchar_t *>(ToBufferAddress(image, address));
    if (!value || address % alignof(wchar_t) != 0)
        return nullptr;
    const size_t count = (image->ImageSize - (address - image->ImageBase)) / sizeof(wchar_t);
    for (size_t i = 0; i < count; ++i)
        if (value[i] == L'\0')
            return value;
    return nullptr;
}

std::wstring Escape(const std::wstring &value)
{
    std::wstring result;
    for (wchar_t c : value)
    {
        switch (c)
        {
        case L'\\':
            result += L"\\\\";
            break;
        case L'\t':
            result += L"\\t";
            break;
        case L'\r':
            result += L"\\r";
            break;
        case L'\n':
            result += L"\\n";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}

std::wstring Unescape(const std::wstring &value)
{
    std::wstring result;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == L'\\' && i + 1 < value.size())
        {
            switch (value[i + 1])
            {
            case L'\\':
                result += L'\\';
                ++i;
                continue;
            case L't':
                result += L'\t';
                ++i;
                continue;
            case L'r':
                result += L'\r';
                ++i;
                continue;
            case L'n':
                result += L'\n';
                ++i;
                continue;
            default:
                break; // Preserve unknown escapes verbatim.
            }
        }
        result += value[i];
    }
    return result;
}

bool IsEnglish(const wchar_t *value)
{
    size_t english = 0;
    size_t other = 0;
    for (; *value; ++value)
    {
        const wchar_t c = *value;
        if (c == L' ' || c == L'\n' || c == L'\r' || c == L'\t' || (c >= L'0' && c <= L'9'))
            continue;
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'))
            ++english;
        else
            ++other;
    }
    return english > other;
}

std::wstring LanguagePath(const wchar_t *language, const wchar_t *domain)
{
    return std::wstring(language) + L"\\lang\\" + domain + L".txt";
}

bool LoadTranslations(const std::wstring &path, Translations &translations)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
    {
        PrintMessage(L"无法打开文件：%ls\n", path.c_str());
        return false;
    }
    translations.clear();
    std::string bytes;
    size_t lineNumber = 0;
    while (std::getline(input, bytes))
    {
        if (++lineNumber == 1 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
            bytes.erase(0, 3);
        std::wstring line;
        if (!DecodeUtf8(bytes, line))
        {
            PrintMessage(L"无效的 UTF-8 编码：%ls，第 %zu 行\n", path.c_str(), lineNumber);
            return false;
        }
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();
        if (line.empty() || line.front() == L'#')
            continue;
        const auto separator = line.find(L'=');
        if (separator == std::wstring::npos || line.find(L'\0') != std::wstring::npos)
        {
            PrintMessage(L"格式错误：%ls\n", path.c_str());
            return false;
        }
        translations[line.substr(0, separator)] = Unescape(line.substr(separator + 1));
    }
    if (input.bad() || !input.eof())
    {
        PrintMessage(L"读取语言文件失败：%ls\n", path.c_str());
        return false;
    }
    PrintMessage(L"读取完毕，共 %zu 条记录。\n", translations.size());
    return true;
}

LocalizationEntry *FindLanguage(PE_HANDLE image, const Translations &translations, bool japanese)
{
    size_t bestMatches = 0;
    size_t bestLanguageCount = 0;
    LocalizationEntry *best = nullptr;
    if (image->ImageSize < sizeof(LocalizationEntry))
        return nullptr;
    for (size_t offset = 0; offset <= image->ImageSize - sizeof(LocalizationEntry); offset += sizeof(size_t))
    {
        auto *candidate = reinterpret_cast<LocalizationEntry *>(image->buffer + offset);
        auto *entry = candidate;
        size_t english = 0, other = 0, matches = 0, misses = 0;
        while (Contains(image, entry, sizeof(*entry)) && entry->id != 0)
        {
            const auto *name = GetString(image, entry->name);
            const auto *value = GetString(image, entry->text);
            if (!name || !value || !IsEnglish(name))
                break;
            if (IsEnglish(value))
                ++english;
            else
                ++other;
            if (translations.find(name) != translations.end())
                ++matches;
            else
                ++misses;
            ++entry;
        }
        const auto languageCount = japanese ? other : english;
        const auto oppositeCount = japanese ? english : other;
        if (matches > misses && matches > bestMatches && languageCount > oppositeCount &&
            languageCount > bestLanguageCount)
        {
            best = candidate;
            bestMatches = matches;
            bestLanguageCount = languageCount;
        }
    }
    return best;
}

bool ExportLanguage(PE_HANDLE image, LocalizationEntry *entry, const std::wstring &path)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output)
        return false;
    while (Contains(image, entry, sizeof(*entry)))
    {
        const auto *name = GetString(image, entry->name);
        const auto *value = GetString(image, entry->text);
        if (!name || !value)
            break;
        const std::wstring line = std::wstring(name) + L"=" + Escape(value) + L"\n";
        std::string bytes;
        if (!EncodeUtf8(line, bytes))
            return false;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ++entry;
    }
    output.close();
    return static_cast<bool>(output);
}

void ReplaceLanguage(PE_HANDLE image, LocalizationEntry *entry, const Translations &translations,
                     MemoryFragmentPool &pool, std::vector<PendingTranslation> &pending)
{
    while (Contains(image, entry, sizeof(*entry)))
    {
        const auto *key = GetString(image, entry->name);
        auto *text = GetString(image, entry->text);
        if (!key || !text)
            break;
        const auto it = translations.find(key);
        if (it == translations.end())
        {
            PrintMessage(L"找不到Key:%ls\n", key);
        }
        else
        {
            const auto &replacement = it->second;
            const size_t oldLength = std::wcslen(text);
            const size_t newLength = replacement.size();
            // Single-letter strings may share storage with shortcut names.
            if (newLength > oldLength || (oldLength == 1 && IsEnglish(text)))
            {
                pending.push_back({entry, replacement});
            }
            else
            {
                std::memcpy(text, replacement.c_str(), (newLength + 1) * sizeof(wchar_t));
                // Preserve the existing allocation policy and its output layout.
                if (oldLength > newLength + 2)
                {
                    auto *remainder = text + newLength + 1;
                    const size_t bytes = (oldLength - newLength - 1) * sizeof(wchar_t);
                    if (*remainder)
                    {
                        std::memset(remainder, 0, bytes);
                        pool.Release(reinterpret_cast<UINT_PTR>(remainder), bytes);
                    }
                }
            }
        }
        ++entry;
    }
}

bool ApplyPending(PE_HANDLE image, MemoryFragmentPool &pool, const std::vector<PendingTranslation> &pending)
{
    for (const auto &item : pending)
    {
        const size_t bytes = (item.value.size() + 1) * sizeof(wchar_t);
        const auto address = pool.Allocate(bytes);
        if (!address)
        {
            PrintMessage(L"【错误】没有足够的空间容纳 %ls\n", item.value.c_str());
            return false;
        }
        item.entry->text = ToVirtualAddress(image, address);
        std::memcpy(reinterpret_cast<void *>(address), item.value.c_str(), bytes);
    }
    return true;
}
} // namespace

bool ProcessLanguages(PE_HANDLE image, const wchar_t *sourceLanguage, const wchar_t *destinationLanguage, const wchar_t *outputLanguage)
{
    if (!image || !image->buffer || !sourceLanguage || (!!destinationLanguage == !!outputLanguage))
        return false;
    const wchar_t *domains[] = {L"Main", L"MainTip", L"Import", L"Option", L"Errdlg"};
    MemoryFragmentPool pool;
    std::vector<PendingTranslation> pending;
    for (const auto *domain : domains)
    {
        Translations translations;
        const auto sourcePath = LanguagePath(sourceLanguage, domain);
        PrintMessage(L"\n原始语言文件：%ls\n", sourcePath.c_str());
        if (!LoadTranslations(sourcePath, translations))
            return false;
        auto *entry = FindLanguage(image, translations, std::wcscmp(sourceLanguage, L"ja") == 0);
        if (!entry)
        {
            PrintMessage(L"无法定位语言表：%ls\n", domain);
            return false;
        }
        if (destinationLanguage)
        {
            const auto path = LanguagePath(destinationLanguage, domain);
            PrintMessage(L"翻译语言文件：%ls\n", path.c_str());
            if (!LoadTranslations(path, translations))
                return false;
            ReplaceLanguage(image, entry, translations, pool, pending);
        }
        else
        {
            const auto path = LanguagePath(outputLanguage, domain);
            PrintMessage(L"输出文件：%ls\n", path.c_str());
            if (!ExportLanguage(image, entry, path))
            {
                PrintMessage(L"写入语言文件失败：%ls\n", path.c_str());
                return false;
            }
        }
    }
    return ApplyPending(image, pool, pending);
}
