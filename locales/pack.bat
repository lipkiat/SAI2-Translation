@echo off
chcp 65001

set "SEVENZIP_PATH=C:\Program Files\7-Zip\7z.exe"
set "PREFIX=sai2-20260702-64bit-"

:: zh-CN
copy /Y ".\en\init\blotmap\Blots.bmp" ".\zh-CN\init\blotmap\晕染.bmp"
copy /Y ".\en\init\blotmap\Blots&Noise.bmp" ".\zh-CN\init\blotmap\晕染＆噪点.bmp"
copy /Y ".\en\init\bristle\Bristle.bmp" ".\zh-CN\init\bristle\圆笔.bmp"
copy /Y ".\en\init\bristle\Flat Bristle.bmp" ".\zh-CN\init\bristle\平笔.bmp"
copy /Y ".\en\init\bristle\Flat Face.bmp" ".\zh-CN\init\bristle\平面.bmp"
copy /Y ".\en\init\brshape\Water Blur.bmp" ".\zh-CN\init\brshape\水彩晕染.bmp"
copy /Y ".\en\init\brushtex\Canvas.bmp" ".\zh-CN\init\brushtex\画布.bmp"
copy /Y ".\en\init\brushtex\Paper.bmp" ".\zh-CN\init\brushtex\画纸.bmp"
copy /Y ".\en\init\papertex\Canvas.bmp" ".\zh-CN\init\papertex\画布.bmp"
copy /Y ".\en\init\papertex\Paper.bmp" ".\zh-CN\init\papertex\画纸.bmp"
copy /Y ".\en\init\papertex\Water Color 1.bmp" ".\zh-CN\init\papertex\水彩１.bmp"
copy /Y ".\en\init\papertex\Water Color 2.bmp" ".\zh-CN\init\papertex\水彩２.bmp"
copy /Y ".\en\init\scatter\Stars.bmp" ".\zh-CN\init\scatter\星.bmp"
call:Pack "zh-CN"

:: zh-TW
move /Y ".\zh-CN\init\blotmap\晕染.bmp" ".\zh-TW\init\blotmap\暈染.bmp"
move /Y ".\zh-CN\init\blotmap\晕染＆噪点.bmp" ".\zh-TW\init\blotmap\暈染＆噪點.bmp"
move /Y ".\zh-CN\init\bristle\圆笔.bmp" ".\zh-TW\init\bristle\圓筆.bmp"
move /Y ".\zh-CN\init\bristle\平笔.bmp" ".\zh-TW\init\bristle\平筆.bmp"
move /Y ".\zh-CN\init\bristle\平面.bmp" ".\zh-TW\init\bristle\平面.bmp"
move /Y ".\zh-CN\init\brshape\水彩晕染.bmp" ".\zh-TW\init\brshape\水彩暈染.bmp"
move /Y ".\zh-CN\init\brushtex\画布.bmp" ".\zh-TW\init\brushtex\畫布.bmp"
move /Y ".\zh-CN\init\brushtex\画纸.bmp" ".\zh-TW\init\brushtex\畫紙.bmp"
move /Y ".\zh-CN\init\papertex\画布.bmp" ".\zh-TW\init\papertex\畫布.bmp"
move /Y ".\zh-CN\init\papertex\画纸.bmp" ".\zh-TW\init\papertex\畫紙.bmp"
move /Y ".\zh-CN\init\papertex\水彩１.bmp" ".\zh-TW\init\papertex\水彩１.bmp"
move /Y ".\zh-CN\init\papertex\水彩２.bmp" ".\zh-TW\init\papertex\水彩２.bmp"
move /Y ".\zh-CN\init\scatter\星.bmp" ".\zh-TW\init\scatter\星.bmp"
call:Pack "zh-TW"
del /s /q "%~dp0\zh-TW\init\*.bmp"
pause
exit

:Pack
".\patcher.exe" ".\sai2.exe.bak" ".\%~1"
set "SOURCE_PATH=".\%~1\init\" ".\%~1\sai2.exe" ".\%~1\sai2.ini" ".\%~1\history.txt""
set "OUTPUT_ARCHIVE=%~dp0%PREFIX%%~1.zip"
"%SEVENZIP_PATH%" a -tzip "%OUTPUT_ARCHIVE%" %SOURCE_PATH%
del /s /q ".\%~1\sai2.exe"
goto:eof
