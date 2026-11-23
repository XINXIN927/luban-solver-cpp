@echo off
REM 一键编译全部版本：命令行求解器 + Win32 GUI
REM 依赖：MinGW-w64 g++（默认取 PATH 中的 g++；不在 PATH 时可设置环境变量 CXX 为编译器绝对路径）
REM GUI 编译使用 bgui.js（Node.js 脚本，用 stdin 喂源码绕过体积限制）

set GCC=g++
if defined CXX set GCC=%CXX%

echo ============================================
echo [1/2] 编译命令行求解器（静态链接）
echo ============================================
"%GCC%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
  luban_cli_solver.cpp luban_cli_main.cpp ^
  -o luban_cli_solver.exe
if %errorlevel%==0 (echo   -> luban_cli_solver.exe OK) else (echo   -> CLI BUILD FAILED)

echo.
echo ============================================
echo [2/2] 编译 Win32 GUI（静态链接，含 OpenGL）
echo ============================================
where node >nul 2>nul
if %errorlevel%==0 (
  node bgui.js -O2
) else (
  echo   Node.js 未找到，改用 g++ 直接编译（可能需要关闭某些启发式拦截）...
  "%GCC%" -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    luban_win32_gui.cpp luban_cli_solver.cpp ^
    -lopengl32 -lgdi32 -lcomctl32 -luuid -mwindows -o luban_gui.exe
)
if exist luban_gui.exe (echo   -> luban_gui.exe OK) else (echo   -> GUI BUILD FAILED)

echo.
echo 完成。产物：luban_cli_solver.exe / luban_gui.exe
