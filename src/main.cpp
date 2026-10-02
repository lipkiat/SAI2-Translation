/* Open source: https://github.com/lipkiat/SAI2-Translation */
#include "common.h"
#include "localization.h"
#include "patch.h"

#include <cwchar>
#include <exception>

namespace
{
struct Options
{
    const wchar_t *sourceLanguage = nullptr;
    const wchar_t *destinationLanguage = nullptr;
    const wchar_t *originalFile = nullptr;
    const wchar_t *outputLanguage = nullptr;
    const wchar_t *outputFile = nullptr;
    bool crack = false;
};

/* 命令行
/SourceLanguage			源语言。
/DestinationLanguage	目标语言的目录名。
/OriginalFile			原始“sai2.exe”的可执行文件。
/OutputLanguage			输出源语言到指定目录，需要自行创建目录和 lang 子目录。
/OutputFile				输出已修补的新文件。
/crack					破解。
*/
bool ParseOptions(int argc, wchar_t *argv[], Options &options)
{
    struct ValueOption
    {
        const wchar_t *name;
        const wchar_t **value;
    };
    const ValueOption valueOptions[] = {
        {L"SourceLanguage", &options.sourceLanguage},
        {L"DestinationLanguage", &options.destinationLanguage},
        {L"OriginalFile", &options.originalFile},
        {L"OutputLanguage", &options.outputLanguage},
        {L"OutputFile", &options.outputFile},
    };
    for (int i = 1; i < argc; ++i)
    {
        const auto *argument = argv[i];
        if (argument[0] != L'-' && argument[0] != L'/')
        {
            PrintMessage(L"错误：无效参数 %ls\n", argument);
            return false;
        }
        const auto *name = argument + 1;
        if (std::wcscmp(name, L"crack") == 0)
        {
            options.crack = true;
            continue;
        }
        const wchar_t **destination = nullptr;
        for (const auto &option : valueOptions)
            if (std::wcscmp(name, option.name) == 0)
                destination = option.value;
        if (!destination)
        {
            PrintMessage(L"错误：未知选项 %ls\n", name);
            return false;
        }
        if (i + 1 >= argc || !argv[i + 1][0] || argv[i + 1][0] == L'/' || argv[i + 1][0] == L'-')
        {
            PrintMessage(L"错误：选项 %ls 的后面必须跟参数\n", name);
            return false;
        }
        *destination = argv[++i];
    }
    if ((options.destinationLanguage || options.outputLanguage) && !options.sourceLanguage)
    {
        PrintMessage(L"错误：请指定源语言\n");
        return false;
    }
    if (options.sourceLanguage && (!!options.destinationLanguage == !!options.outputLanguage))
    {
        PrintMessage(L"错误：请指定一个目标语言或输出目录\n");
        return false;
    }
    return true;
}

int Run(int argc, wchar_t *argv[])
{
    Options options;
    if (!ParseOptions(argc, argv, options))
        return 1;
    if (!options.originalFile)
    {
        PrintMessage(L"错误：未指定可执行文件\n");
        return 2;
    }
    PrintMessage(L"原始文件：%ls\n", options.originalFile);
    PE_IMAGE image;
    if (!LoadPEFile(&image, options.originalFile))
    {
        PrintMessage(L"加载 PE 文件失败\n");
        return 1;
    }
    if (options.crack && !ApplyBinaryPatch(&image))
    {
        PrintMessage(L"补丁应用失败：无法定位有效的特征码或目标地址\n");
        return 1;
    }
    if (options.sourceLanguage && !ProcessLanguages(&image, options.sourceLanguage,
                                                    options.destinationLanguage, options.outputLanguage))
        return 1;
    if (options.outputFile && !SavePEFile(&image, options.outputFile))
    {
        PrintMessage(L"保存 PE 文件失败：%ls\n", options.outputFile);
        return 1;
    }
    PrintMessage(L"\nDone.\n");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t *argv[])
{
    try
    {
        return Run(argc, argv);
    }
    catch (const std::exception &error)
    {
        PrintMessage(L"错误：%hs\n", error.what());
        return 1;
    }
}
