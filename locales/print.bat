@echo off
chcp 65001
:: 打印出新的原语言，以方便对比

set /p SourceLanguage=输入原语言（ja / en）: 
mkdir "new\lang"
call "patcher.exe" /OriginalFile ".\sai2.exe.bak" /SourceLanguage "%SourceLanguage%" /OutputLanguage "new"

pause
