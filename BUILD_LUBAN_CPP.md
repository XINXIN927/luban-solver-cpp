# 鲁班锁 C++ 核心构建说明

当前 C++ 部分分为五块：

- `luban_cli_solver.cpp`：核心求解实现，提供 `solveLubanLock()`
- `luban_core_api.h`：GUI/CLI 共用的结构化 API
- `luban_cli_main.cpp`：命令行入口
- `luban_win32_gui.cpp`：轻量 Win32 原生界面 + OpenGL 体素显示
- `gui/`：Qt Widgets 桌面 GUI 骨架

## 直接用 g++ 编译

```powershell
g++ -std=c++17 -O2 -Wall -Wextra luban_cli_solver.cpp luban_cli_main.cpp -o luban_cli_solver.exe
```

运行：

```powershell
.\luban_cli_solver.exe 733 669 1004 366 99 1023
.\luban_cli_solver.exe --length 8 --codes 733,669,1004,366,99,1023
```

## 轻量 Win32 + OpenGL GUI

这是推荐的 Windows 发布版路线：

```powershell
g++ -std=c++17 -O2 -Wall -Wextra luban_cli_solver.cpp luban_win32_gui.cpp -municode -mwindows -lopengl32 -lcomctl32 -lgdi32 -luser32 -o luban_win32_gui.exe
```

特点：

- 不依赖 Qt DLL
- 使用 Windows 原生控件
- 使用系统自带 `opengl32` 绘制体素
- 支持求解、步骤列表、上一步/下一步、自动拆解/自动拼装
- 鼠标拖动旋转视角，滚轮缩放

## API 测试

```powershell
g++ -std=c++17 -O2 -Wall -Wextra luban_cli_solver.cpp luban_core_api_test.cpp -o luban_core_api_test.exe
.\luban_core_api_test.exe
```

## CMake 构建

项目已提供 `CMakeLists.txt`。安装 CMake 后可用：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

CMake 会生成这些目标：

- `luban_core`：核心库
- `luban_cli_solver`：命令行程序
- `luban_core_api_test`：API 测试程序
- `luban_win32_gui`：Windows 轻量 GUI，使用 Win32 + OpenGL
- `luban_qt_gui`：Qt 桌面 GUI，只有安装 Qt5/Qt6 Widgets 后才会生成

当前开发环境暂时没有检测到 `cmake`、`qmake` 或 `qmake6`。核心库和 CLI 已用 g++ 验证通过；Qt GUI 源码已就绪，安装 Qt/CMake 后再构建 GUI。

## qmake / Qt Creator 构建 GUI

项目也提供了 `luban_qt_gui.pro`，可以直接用 Qt Creator 打开，或用命令行：

```powershell
qmake luban_qt_gui.pro
nmake
```

如果使用 MinGW 版 Qt，则通常是：

```powershell
qmake luban_qt_gui.pro
mingw32-make
```

## Qt GUI 接入方式

Qt 工程后续只需要链接 `luban_core`，并包含：

```cpp
#include "luban_core_api.h"
```

调用：

```cpp
LubanSolveSummary result = solveLubanLock({733, 669, 1004, 366, 99, 1023}, 6);
```

GUI 常用字段：

- `result.pieces`：每根柱子的编码、槽位、旋转方向和体素坐标，可直接用于 3D 显示
- `result.moves`：结构化拆解动作，包含柱子组、方向、步数、是否移除、是否旋转、旋转轴心
- `result.steps`：同一批动作的中文说明文本，可用于右侧步骤列表
- `result.rotationLock`、`result.rotatePiece`、`result.preRotation`：旋转锁显示和第一步旋转解锁信息

## 当前 Qt GUI 骨架

已新增：

- `gui/qt_main.cpp`
- `gui/MainWindow.h`
- `gui/MainWindow.cpp`
- `gui/VoxelViewWidget.h`
- `gui/VoxelViewWidget.cpp`

当前界面包含：

- 6 个柱子编码输入框
- 柱长选择：`6 / 8 / 10 / 12`
- 求解按钮
- 锁体体素预览区
- 鼠标左右拖动改变观察角度
- 上一步 / 下一步
- 自动拆解 / 自动拼装
- 重置当前动画
- 结果摘要
- 柱子体素统计表
- 结构化拆解动作表
- 中文步骤说明

当前体素预览使用 `QPainter` 绘制等轴测方块，优点是依赖少、容易先跑通 GUI。`VoxelViewWidget` 已经可以根据 `result.moves` 应用平移和旋转动作，步骤表会随播放同步选中。后续如果需要更接近 BurrTools 的效果，可以在同一个 `VoxelViewWidget` 位置升级为 OpenGL 视图。
