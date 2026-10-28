@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
cmake --build build\Desktop_Qt_6_12_0_MSVC2022_64bit_Debug --target banchoxterm banchoxterm_tests --parallel 4 > build\bell-build.log 2>&1
if errorlevel 1 exit /b %errorlevel%
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=C:\Windows\Fonts"
build\Desktop_Qt_6_12_0_MSVC2022_64bit_Debug\banchoxterm_tests.exe testTerminalBellNotification -o build\bell-tests.txt,txt
