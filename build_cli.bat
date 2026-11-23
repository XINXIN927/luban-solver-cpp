@echo off
cd /d "%~dp0"
set GCC=g++
if defined CXX set GCC=%CXX%

echo Compiling CLI solver...
"%GCC%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ luban_cli_solver.cpp luban_cli_main.cpp -o luban_cli_solver.exe
if %errorlevel%==0 (echo SUCCESS: luban_cli_solver.exe) else (echo BUILD FAILED)
pause
