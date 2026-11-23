#include "luban_core_api.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static vector<int> parseCodes(const string &text) {
  string s = text;
  for (char &ch : s) {
    if (ch == ',' || ch == ';' || ch == '|') ch = ' ';
  }
  stringstream ss(s);
  vector<int> codes;
  int value;
  while (ss >> value) codes.push_back(value);
  return codes;
}

static void printUsage() {
  cout << "用法:\n";
  cout << "  luban_cli_solver.exe 733 669 1004 366 99 1023\n";
  cout << "  luban_cli_solver.exe --length 8 --codes 733,669,1004,366,99,1023\n";
  cout << "枚举模式（给定柱子池，搜索所有可拼可拆组合并统计数量）:\n";
  cout << "  luban_cli_solver.exe --enumerate --pool \"0x1 17x2 34x1 51x2 ...\"\n";
  cout << "  luban_cli_solver.exe --enumerate --length 6 --pool \"...\" --expect 314\n";
  cout << "  luban_cli_solver.exe --enumerate --resume --pool \"...\"\n";
  cout << "  --pool \"CODExN ...\" : 每种柱 CODE 提供 N 个（可锯柱拼装数据风格）\n";
  cout << "  --expect N          : 给出 BurrTools 的解数，程序会对比一致率\n";
  cout << "  --resume            : 从上次停止的断点继续枚举\n";
}

// 从 "CODExN ..." 解析出柱子池（每种 code 的若干份）
static vector<int> parsePool(const string &text) {
  vector<int> pool;
  string s = text;
  for (char &ch : s) if (ch == ',') ch = ' ';
  stringstream ss(s);
  string tok;
  while (ss >> tok) {
    auto xpos = tok.find('x');
    if (xpos != string::npos) {
      int code = atoi(tok.substr(0, xpos).c_str());
      int n = atoi(tok.substr(xpos + 1).c_str());
      for (int k = 0; k < n; ++k) pool.push_back(code);
    } else {
      pool.push_back(atoi(tok.c_str()));
    }
  }
  return pool;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);  // 禁用stdout缓冲，实时输出进度
  int length = 6;
  vector<int> codes;
  bool enumerate = false;
  bool resume = false;
  string poolText;
  string poolFile;
  int expect = -1;
  bool fast = false;
  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if ((arg == "--length" || arg == "-l") && i + 1 < argc) {
      length = atoi(argv[++i]);
    } else if ((arg == "--codes" || arg == "-c") && i + 1 < argc) {
      codes = parseCodes(argv[++i]);
    } else if (arg == "--enumerate" || arg == "-e") {
      enumerate = true;
    } else if ((arg == "--pool" || arg == "-p") && i + 1 < argc) {
      poolText = argv[++i];
    } else if ((arg == "--pool-file" || arg == "-f") && i + 1 < argc) {
      poolFile = argv[++i];
    } else if ((arg == "--expect" || arg == "-x") && i + 1 < argc) {
      expect = atoi(argv[++i]);
    } else if (arg == "--fast") {
      fast = true;
    } else if (arg == "--resume") {
      resume = true;
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      vector<int> more = parseCodes(arg);
      codes.insert(codes.end(), more.begin(), more.end());
    }
  }

  if (enumerate) {
    if (poolFile.empty() && poolText.empty()) {
      cerr << "枚举模式需要 --pool \"CODExN ...\" 或 --pool-file FILE\n";
      printUsage();
      return 1;
    }
    if (poolFile.empty()) poolFile = poolFile; // no-op
    string src = poolText;
    if (!poolFile.empty()) {
      ifstream fin(poolFile);
      string line;
      string allLines;
      while (getline(fin, line)) {
        auto h = line.find('#'); if (h != string::npos) line = line.substr(0, h);
        if (!line.empty()) { if (!allLines.empty()) allLines += " "; allLines += line; }
      }
      src = allLines;
      fin.close();
    }
    vector<int> pool = parsePool(src);
    if (pool.empty()) {
      cerr << "柱子池为空。\n";
      return 1;
    }
    LubanEnumCheckpoint cp;
    if (resume) {
      vector<int> sortedPool = pool;
      sort(sortedPool.begin(), sortedPool.end());
      cp = loadEnumCheckpoint(sortedPool, length);
      if (cp.valid) cout << "从断点续搜：已跳过前 " << cp.processedCount << " 个组合\n";
      else cout << "未找到有效断点，从头开始\n";
    }
    enumerateLubanPool(pool, length, expect, fast, cp);
    return 0;
  }

  LubanSolveSummary result = solveLubanLock(codes, length);
  if (!result.inputOk) {
    printUsage();
    if (!result.error.empty()) cerr << result.error << "\n";
    return 1;
  }

  cout << "鲁班锁六柱 CLI 求解器\n";
  cout << "编码: ";
  for (size_t i = 0; i < result.codes.size(); ++i) {
    cout << (i ? "," : "") << result.codes[i];
  }
  cout << "\n柱长: 2x2x" << result.length << "\n";
  cout << "用时: " << result.elapsedMs << " ms\n";
  cout << "目标: " << result.targetName << "；必填 " << result.requiredCells << "；允许 " << result.allowedCells << "\n";
  cout << "搜索节点: " << result.searchNodes << "；验证装配候选: " << result.checkedAssemblies;
  if (result.skippedDuplicates > 0)
    cout << "（去重跳过 " << result.skippedDuplicates << "，实际验证 " << (result.checkedAssemblies - result.skippedDuplicates) << " 个唯一装配）";
  cout << "\n";

  if (result.assemblyOk && !result.pieces.empty()) {
    cout << "\n===LOCK_DATA_START===\n";
    for (size_t i = 0; i < result.pieces.size(); ++i) {
      cout << "{\"piece\":" << result.pieces[i].piece << ",\"slot\":" << result.pieces[i].slot << ",\"rot\":" << result.pieces[i].rotationIndex << ",\"cells\":[";
      for (size_t j = 0; j < result.pieces[i].cells.size(); ++j) {
        if (j) cout << ",";
        cout << "[" << result.pieces[i].cells[j].x << "," << result.pieces[i].cells[j].y << "," << result.pieces[i].cells[j].z << "]";
      }
      cout << "]}";
      if (i + 1 < result.pieces.size()) cout << "\n";
    }
    cout << "\n===LOCK_DATA_END===\n\n";
  }

  if (!result.assemblyOk) {
    cout << "结果: 失败\n";
    cout << "原因: " << result.error << "\n";
    return 2;
  }
  if (!result.disassemblable) {
    cout << "结果: 可拼装，但暂未找到拆解路径\n";
    if (!result.error.empty()) cout << "原因: " << result.error << "\n";
    return 3;
  }

  cout << "结果: 可拼装，可拆解\n";
  cout << "旋转锁: " << (result.rotationLock ? "是" : "否") << "\n";
  if (result.rotationLock) {
    cout << "旋转柱: " << result.rotatePiece << "；轴心: "
         << result.pivotX << "," << result.pivotY << "," << result.pivotZ
         << "；拆解预旋转: " << result.preRotation << "\n";
  }
  cout << "难度: 第一阶段 " << result.firstLevel
       << "；最大阶段 " << result.maxStage
       << "；阶段数 " << result.exitCount
       << "；总动作 " << result.totalMoves << "\n";
  cout << "拆解步骤:\n";
  for (size_t i = 0; i < result.steps.size(); ++i) {
    cout << "  " << (i + 1) << ". " << result.steps[i].text << "\n";
  }
  return 0;
}
