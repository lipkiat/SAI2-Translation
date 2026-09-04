## Official version
- Japanese: https://www.systemax.jp/ja/sai/devdept.html
- English: https://www.systemax.jp/en/sai/devdept.html

## Command-line usage for the patch tool
```
:: Navigate to the sai2 directory
cd /d <the directory where your sai2.exe is located>

:: Back up the original file
rename "sai2.exe" "sai2.exe.bak"

:: Ensure that patcher.exe exists in the current directory, then apply the patch for cracking and translation. The patcher will save the new file as sai2.exe.
patcher /crack /OriginalFile "sai2.exe.bak" /OutputFile "sai2.exe" /SourceLanguage "ja" /DestinationLanguage <your language code, e.g., zh-CN>

```
