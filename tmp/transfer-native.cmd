@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
cmake --build build\Desktop_Qt_6_12_0_MSVC2022_64bit_Debug --target banchoxterm banchoxterm_tests --parallel 4 > build\transfer-build.log 2>&1
