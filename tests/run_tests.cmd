@echo off
rem Builds and runs the C++ engine's strategy tests (32-bit, like the plugin).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
cd /d "%~dp0.."
if not exist build mkdir build
cl /nologo /O2 /EHsc /W3 /std:c++17 tests\ai_tests.cpp /Fobuild\ /Fe:build\ai_tests.exe || exit /b 1
build\ai_tests.exe
