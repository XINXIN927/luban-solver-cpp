@echo off
REM 编译 Win32 GUI + 核心求解器（完全静态链接，无外部 DLL 依赖）
REM 编译产物可直接发给用户运行，不依赖 MSYS2 / MinGW 运行时
echo Compiling...
g++ -std=c++17 -O2 -c luban_win32_gui.cpp -o luban_win32_gui.o
g++ -std=c++17 -O2 -c luban_cli_solver.cpp -o luban_cli_solver.o

REM 使用 g++ 链接（-Bstatic 模式下 ld 链接 libstdc++.a 有 unresolved reference）
g++ -static -static-libgcc -static-libstdc++ -Wl,--subsystem=windows -o luban_win32_gui.exe ^
  luban_win32_gui.o luban_cli_solver.o ^
  -lopengl32 -lglu32 -lwinmm -lwininet -lcomctl32 -lcomdlg32 ^
  -lpthread -lws2_32 ^
  -lkernel32 -luser32 -lgdi32 -lshell32 -ladvapi32

del luban_win32_gui.o luban_cli_solver.o 2>nul
echo Done: luban_win32_gui.exe (fully static, no DLL dependencies)
