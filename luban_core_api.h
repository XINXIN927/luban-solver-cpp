#ifndef LUBAN_CORE_API_H
#define LUBAN_CORE_API_H

#include <functional>
#include <string>
#include <vector>

struct LubanCell {
  int x = 0;
  int y = 0;
  int z = 0;
};

struct LubanPieceView {
  int piece = 0;
  int code = 0;
  int slot = 0;
  int rotationIndex = 0;
  std::vector<LubanCell> cells;
};

struct LubanStep {
  std::string text;
};

struct LubanMove {
  std::string text;
  std::vector<int> group;
  std::string direction;
  int dx = 0;
  int dy = 0;
  int dz = 0;
  int steps = 0;
  bool exit = false;
  bool finalRemoval = false;
  bool rotate = false;
  LubanCell pivot;
  int rotationDir = 0;
  char rotationAxis = 'y';  // 'x', 'y', or 'z'
};

struct LubanSolveSummary {
  bool inputOk = false;
  bool assemblyOk = false;
  bool disassemblable = false;
  bool rotationLock = false;
  bool searchLimited = false;
  bool deadlocked = false;
  std::string error;

  int length = 6;
  std::vector<int> codes;
  double elapsedMs = 0.0;

  std::string targetName;
  int requiredCells = 0;
  int allowedCells = 0;
  int searchNodes = 0;
  int checkedAssemblies = 0;
  int skippedDuplicates = 0;  // 整体对称去重跳过的等价装配数

  int firstLevel = 0;
  int maxStage = 0;
  int exitCount = 0;
  int totalMoves = 0;

  int rotatePiece = -1;
  int pivotX = 0;
  int pivotY = 0;
  int pivotZ = 0;
  std::string preRotation;

  std::vector<LubanPieceView> pieces;
  std::vector<LubanStep> steps;
  std::vector<LubanMove> moves;
};

LubanSolveSummary solveLubanLock(const std::vector<int> &codes, int length);

struct LubanEnumResult {
  int totalCombos = 0;
  int distinctCombos = 0;
  int assemblableCombos = 0;
  int disassemblableCombos = 0;     // 可拼且可拆组合（非旋转+旋转）
  int totalSolutions = 0;           // 可拆装配方案总数（含朝向）
  int noRotCombos = 0;              // 非旋转锁组合（直接可拆）
  int rotCombos = 0;                // 旋转锁组合（需旋转解锁才能拆）
  int noRotSolutions = 0;           // 非旋转可拆装配方案数（含朝向）
  int rotSolutions = 0;             // 旋转可拆装配方案数（含朝向）
  double elapsedMs = 0;
  // 诊断：汇编器找到的候选数统计
  int totalAssembliesChecked = 0;   // 所有组合累加的 checkedAssemblies
  int maxCandidatesPerCombo = 0;    // 单个组合最多找到的装配候选数
  int comboWithMultiCandidates = 0; // 找到 >1 个候选的组合数
  int maxDisasmPerCombo = 0;        // 单个组合最多可拆的装配数（非旋转+旋转合计）
};

struct LubanEnumCheckpoint {
  bool valid = false;
  std::vector<int> pool;
  int length = 6;
  int processedCount = 0;
  int noRotSolutions = 0;
  int rotSolutions = 0;
  int noRotCombos = 0;
  int rotCombos = 0;
  int totalSolutions = 0;
  std::vector<std::string> nonRotLines;
  std::vector<std::string> rotLines;
  std::vector<std::string> assemblyLines;
};

bool saveEnumCheckpoint(const LubanEnumCheckpoint &cp, const std::string &path = "enum_checkpoint.txt");
LubanEnumCheckpoint loadEnumCheckpoint(const std::vector<int> &pool, int length, const std::string &path = "enum_checkpoint.txt");

LubanEnumResult enumerateLubanPool(const std::vector<int> &pool, int length, int expect, bool fast = false, const LubanEnumCheckpoint &resume = LubanEnumCheckpoint{});

// 枚举进度回调（GUI 设置后，每200个组合调用一次）
// 参数: processed, nonRotCombos, rotCombos, totalSolutions
using EnumProgressCallback = std::function<void(int, int, int, int)>;
extern EnumProgressCallback gEnumProgressCallback;
extern volatile bool gEnumStopRequested;  // GUI 设置为 true 可中断枚举

#endif
