@echo off
REM Build Win32 GUI + core solver (fully static, no external DLL)
echo Compiling icon resource...
windres -O coff -o res\app.o res\app.rc
if errorlevel 1 (
  echo Resource build failed.
  exit /b 1
)
echo Compiling...
g++ -std=c++17 -O2 -c luban_win32_gui.cpp -o luban_win32_gui.o
g++ -std=c++17 -O2 -c luban_cli_solver.cpp -o luban_cli_solver.o

g++ -static -static-libgcc -static-libstdc++ -Wl,--subsystem=windows -o luban_win32_gui.exe ^
  res\app.o luban_win32_gui.o luban_cli_solver.o ^
  -lopengl32 -lglu32 -lwinmm -lwininet -lcomctl32 -lcomdlg32 ^
  -lpthread -lws2_32 ^
  -lkernel32 -luser32 -lgdi32 -lshell32 -ladvapi32

del luban_win32_gui.o luban_cli_solver.o res\app.o 2>nul
echo Done: luban_win32_gui.exe (fully static, no DLL dependencies)