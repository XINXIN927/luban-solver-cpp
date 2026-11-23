# luban-solver-cpp

六柱鲁班锁（Burr Puzzle）求解器，C++17 实现，包含命令行求解器与 Win32 + OpenGL 桌面界面。

## 简介

程序接收六根柱子的编码，完成装配判定与可拆性求解，输出结构化拆解动作、中文步骤说明和体素坐标；并支持给定柱子池的枚举模式，统计可拼可拆组合数量。

- 核心求解：`solveLubanLock()`，GUI / CLI 共用同一套结构化 API
- 命令行：支持单锁求解与全池枚举（可断点续算、与参考解数对账）
- 图形界面：Windows 原生控件 + 系统自带 `opengl32` 绘制体素，支持拖动旋转、滚轮缩放、上一步/下一步、自动拆解/拼装

## 目录文件

| 文件 | 说明 |
| --- | --- |
| `luban_cli_solver.cpp` | 核心求解实现（装配枚举、拆解 BFS、去重、统计） |
| `luban_core_api.h` | GUI / CLI 共用的结构化 API（结果、动作、步骤、体素） |
| `luban_cli_main.cpp` | 命令行入口（单锁求解 / 枚举模式） |
| `luban_win32_gui.cpp` | Win32 原生界面 + OpenGL 体素显示 |
| `bgui.js` | Node 编译辅助脚本：以 stdin 喂源码给 g++，规避大文件直接编译的启发式拦截 |
| `build_all.bat` | 一键编译 CLI + GUI（静态链接） |
| `build_cli.bat` | 仅编译命令行求解器 |
| `build_win32_gui.bat` | 编译 Win32 GUI |
| `BUILD_LUBAN_CPP.md` | 分模块构建、API 字段与 Qt 骨架说明 |
| `依赖与编译说明.txt` | 依赖环境、静态链接要点与踩坑记录 |

## 编译

### 环境要求

- 编译器：MinGW-w64 g++（支持 C++17，推荐 MSYS2 UCRT64）
- GUI 链接系统库：`opengl32`、`gdi32`、`comctl32` 等 Windows 自带库
- `bgui.js` 需要 Node.js（仅 GUI 编译时使用；缺失时脚本回退到 g++ 直接编译）

编译器默认取 PATH 中的 `g++`；若 MinGW 不在 PATH，可设置环境变量 `CXX` 指向编译器绝对路径。

### 命令行求解器

```bash
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    luban_cli_solver.cpp luban_cli_main.cpp -o luban_cli_solver.exe
```

或直接运行 `build_cli.bat`。

### Win32 GUI（静态链接，无外部 DLL）

```bash
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    luban_win32_gui.cpp luban_cli_solver.cpp ^
    -lopengl32 -lgdi32 -lcomctl32 -luuid -mwindows -o luban_gui.exe
```

或运行 `build_all.bat` 一次编译全部版本。

> 发布分发包请使用 `-static` 静态链接；动态链接版会依赖 `libgcc_s_seh-1.dll` / `libstdc++-6.dll`。

## 使用

### 单锁求解

```bash
./luban_cli_solver.exe 733 669 1004 366 99 1023
./luban_cli_solver.exe --length 8 --codes 733,669,1004,366,99,1023
```

### 枚举模式（给定柱子池，搜索所有可拼可拆组合）

```bash
./luban_cli_solver.exe --enumerate --pool "0x1 17x2 34x1 51x2 ..."
./luban_cli_solver.exe --enumerate --length 6 --pool "..." --expect 314
./luban_cli_solver.exe --enumerate --resume --pool "..."
```

- `--pool "CODExN ..."`：每种柱码提供 N 根
- `--expect N`：给出参考解数，程序对比一致率
- `--resume`：从上次断点继续枚举

### 图形界面

编译生成 `luban_gui.exe` 后双击运行：输入六个柱码、选择柱长，点击求解；在体素区拖动旋转视角，滚轮缩放，使用步骤按钮逐步播放拆解/拼装。

## 主要 API 字段

`LubanSolveSummary`（见 `luban_core_api.h`）：

- `pieces`：每根柱子的编码、槽位、旋转方向与体素坐标
- `moves`：结构化拆解动作（柱组、方向、步数、是否移除、是否旋转、旋转轴心）
- `steps`：同一批动作的中文说明文本
- `rotationLock` / `rotatePiece` / `preRotation`：旋转锁显示与首步旋转解锁信息

## 运行环境（仅使用编译好的 exe）

- Windows 10 / 11（自带 UCRT 运行库）
- OpenGL 兼容显卡（集成显卡即可）
- 静态链接版无需安装 .NET、MinGW 或额外 DLL

## 已知注意事项

- 拆解 BFS 中曾有一处对 `vector<Node>` 元素的悬空引用，在 `-O2` 下会被误编译，已改为值拷贝；当前源码为修复后版本。
- 个别环境对直接 g++ 编译大文件有启发式拦截，故 GUI 编译经 `bgui.js` 以 stdin 喂源码。

## 许可证

本项目基于 [MIT License](LICENSE) 开源。
