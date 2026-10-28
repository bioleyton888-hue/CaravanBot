@echo off
rem Builds CaravanBot.dll (32-bit, for xNVSE) into build\
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /LD /O2 /MT /EHsc /W3 /std:c++17 src\CaravanBot.cpp /Fobuild\ /Fdbuild\ /Fe:build\CaravanBot.dll /link user32.lib kernel32.lib
