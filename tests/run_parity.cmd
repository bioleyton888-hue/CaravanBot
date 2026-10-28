@echo off
rem Checks that the C++ engine picks the same moves as tools\caravan_ai.py on the saved turns
rem in captures\ (32-bit, like the plugin).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
cd /d "%~dp0.."
if not exist build mkdir build
python -I tools\export_parity.py captures build\parity.txt || exit /b 1
cl /nologo /O2 /EHsc /W3 /std:c++17 tests\ai_parity.cpp /Fobuild\ /Fe:build\ai_parity.exe || exit /b 1
build\ai_parity.exe build\parity.txt
