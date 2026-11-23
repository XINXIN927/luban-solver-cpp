#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "luban_core_api.h"

using namespace std;

struct Vec3 {
  int x = 0;
  int y = 0;
  int z = 0;
};

struct Dir {
  const char *name;
  int dx;
  int dy;
  int dz;
};

struct PiecePlacement {
  int code = 0;
  int piece = 0;
  int slot = 0;
  int rotIndex = 0;
  vector<Vec3> cells;
  vector<uint32_t> keys;
  vector<int> bits;
};

struct Target {
  string name;
  int length = 6;
  unordered_set<uint32_t> required;
  unordered_set<uint32_t> allowed;
};

struct Action {
  vector<int> group;
  Dir dir{ "?", 0, 0, 0 };
  int steps = 0;
  bool exit = false;
  bool finalRemoval = false;
  bool rotate = false;
  Vec3 pivot;
  int rotationDir = 0;
  char rotationAxis = 'y';  // 'x', 'y', or 'z'
};

struct State {
  vector<int> pieces;
  map<int, vector<Vec3>> vox;
};

struct Difficulty {
  int firstLevel = 0;
  int maxStage = 0;
  int exitCount = 0;
  int totalMoves = 0;
  long long score = 0;
};

struct VerifyResult {
  bool ok = false;
  bool rotatedShape = false;
  vector<PiecePlacement> pieces;
  vector<Action> plan;
  Difficulty difficulty;
  int rotatePieceIndex = -1;
  Vec3 pivot;
  int unlockDir = 0;
  char rotateAxis = 'y';  // 'x', 'y', or 'z'
};

static const int BASE_LEN = 6;
static const int ASSEMBLY_MAX_NODES = 2000000;
static int BURR_STATE_LIMIT = 80000;
static long long gEnumComboBudgetMs = -1;   // 枚举模式：单个组合搜索时间预算(ms)，-1=不限
static int gDisasmTimeoutMs = 2000;          // 单次拆解BFS时间上限(ms)，防死锁组合卡死
EnumProgressCallback gEnumProgressCallback = nullptr;  // GUI 进度回调
volatile bool gEnumStopRequested = false;               // GUI 停止标志
static const int BURR_REMOVE_DISTANCE = 30000;

static int popcount64(uint64_t x) {
#ifdef _MSC_VER
  return (int)__popcnt64(x);
#else
  return __builtin_popcountll(x);
#endif
}

static uint64_t cellZobrist(int piece, int nx, int ny, int nz) {
  uint64_t h = (uint64_t)piece;
  h ^= (uint64_t)(nx + 32768) * 0xbf58476d1ce4e5b9ULL;
  h ^= (uint64_t)(ny + 32768) * 0x94d049bb133111ebULL;
  h ^= (uint64_t)(nz + 32768) * 0x517cc1b727220a95ULL;
  return h;
}

static uint64_t stateZobrist(const State &st) {
  int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
  for (int p : st.pieces)
    for (const auto &v : st.vox.at(p)) {
      if (v.x < minX) minX = v.x;
      if (v.y < minY) minY = v.y;
      if (v.z < minZ) minZ = v.z;
    }
  uint64_t h = 0;
  for (int p : st.pieces)
    for (const auto &v : st.vox.at(p))
      h ^= cellZobrist(p, v.x - minX, v.y - minY, v.z - minZ);
  return h;
}

static uint64_t fastStateSig(const State &st) {
  int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
  for (int p : st.pieces)
    for (const auto &v : st.vox.at(p)) {
      if (v.x < minX) minX = v.x;
      if (v.y < minY) minY = v.y;
      if (v.z < minZ) minZ = v.z;
    }
  uint16_t buf[144];
  int n = 0;
  for (int p : st.pieces)
    for (const auto &v : st.vox.at(p)) {
      buf[n++] = (uint16_t)(((p & 0x7) << 12) | ((v.z - minZ) << 8) | ((v.y - minY) << 4) | (v.x - minX));
    }
  sort(buf, buf + n);
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int i = 0; i < n; i++) {
    h ^= (uint64_t)buf[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

struct BitVec {
  static const int WORDS = 5;
  uint64_t w[WORDS] = {};
  void set(int b) { w[b >> 6] |= 1ULL << (b & 63); }
  void reset(int b) { w[b >> 6] &= ~(1ULL << (b & 63)); }
  bool test(int b) const { return (w[b >> 6] >> (b & 63)) & 1; }
  int popcount() const { int n = 0; for (int i = 0; i < WORDS; i++) n += popcount64(w[i]); return n; }
  void clear() { for (int i = 0; i < WORDS; i++) w[i] = 0; }
};

static const array<array<int, 3>, 12> IP = {{
  {{1,1,1}}, {{1,1,2}}, {{1,1,3}}, {{1,1,4}},
  {{0,1,1}}, {{0,1,2}}, {{0,1,3}}, {{0,1,4}},
  {{1,0,2}}, {{1,0,3}}, {{0,0,2}}, {{0,0,3}}
}};

static const vector<array<int, 9>> NR = {
  {1,0,0,0,1,0,0,0,1},{1,0,0,0,0,-1,0,1,0},{1,0,0,0,-1,0,0,0,-1},{1,0,0,0,0,1,0,-1,0},
  {0,0,-1,0,1,0,1,0,0},{0,-1,0,0,0,-1,1,0,0},{0,0,1,0,-1,0,1,0,0},{0,1,0,0,0,1,1,0,0},
  {-1,0,0,0,1,0,0,0,-1},{-1,0,0,0,0,-1,0,-1,0},{-1,0,0,0,-1,0,0,0,1},{-1,0,0,0,0,1,0,1,0},
  {0,0,1,0,1,0,-1,0,0},{0,1,0,0,0,-1,-1,0,0},{0,0,-1,0,-1,0,-1,0,0},{0,-1,0,0,0,1,-1,0,0},
  {0,-1,0,1,0,0,0,0,1},{0,0,1,1,0,0,0,1,0},{0,1,0,1,0,0,0,0,-1},{0,0,-1,1,0,0,0,-1,0},
  {0,1,0,-1,0,0,0,0,1},{0,0,-1,-1,0,0,0,1,0},{0,-1,0,-1,0,0,0,0,-1},{0,0,1,-1,0,0,0,-1,0},
  {-1,0,0,0,1,0,0,0,1},{-1,0,0,0,0,-1,0,1,0},{-1,0,0,0,-1,0,0,0,-1},{-1,0,0,0,0,1,0,-1,0},
  {0,0,-1,0,1,0,-1,0,0},{0,-1,0,0,0,-1,-1,0,0},{0,0,1,0,-1,0,-1,0,0},{0,1,0,0,0,1,-1,0,0},
  {1,0,0,0,1,0,0,0,-1},{1,0,0,0,0,-1,0,-1,0},{1,0,0,0,-1,0,0,0,1},{1,0,0,0,0,1,0,1,0},
  {0,0,1,0,1,0,1,0,0},{0,1,0,0,0,-1,1,0,0},{0,0,-1,0,-1,0,1,0,0},{0,-1,0,0,0,1,1,0,0},
  {0,1,0,-1,0,0,0,0,-1},{0,0,-1,-1,0,0,0,-1,0},{0,-1,0,-1,0,0,0,0,1},{0,0,1,-1,0,0,0,1,0},
  {0,-1,0,1,0,0,0,0,-1},{0,0,1,1,0,0,0,-1,0},{0,1,0,1,0,0,0,0,1},{0,0,-1,1,0,0,0,-1,0}
};

static const vector<Dir> BURR_DIRS = {
  {"+X",1,0,0},{"-X",-1,0,0},{"+Y",0,1,0},{"-Y",0,-1,0},{"+Z",0,0,1},{"-Z",0,0,-1}
};

static const string HOLLOW_TARGET_SHAPE =
  "_____________####__####_______________##____##___#++#__#++#___##____##____##__##++###++++##++++###++##__##____##__##++###++++##++++###++##__##____##____##___#++#__#++#___##____##_______________####__####_____________";

static string key3(int x, int y, int z) {
  return to_string(x) + "," + to_string(y) + "," + to_string(z);
}

static inline int flatIdx(int x, int y, int z, int length) {
  return x + length * y + length * length * z;
}

static int extLen(int length) {
  return max(0, (length - BASE_LEN) / 2);
}

static int det3(const array<int, 9> &m) {
  return m[0] * (m[4] * m[8] - m[5] * m[7])
       - m[1] * (m[3] * m[8] - m[5] * m[6])
       + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

static int assemblySlotDet(int slot) {
  return slot >= 4 ? -1 : 1;
}

static Vec3 assemblySlotMap(int slot, int x, int y, int z, int length) {
  int ext = extLen(length);
  switch (slot) {
    case 0: return {z, x + 1 + ext, y + 2 + ext};
    case 1: return {z, x + 3 + ext, y + 2 + ext};
    case 2: return {x + 1 + ext, y + 2 + ext, z};
    case 3: return {x + 3 + ext, y + 2 + ext, z};
    case 4: return {x + 2 + ext, z, y + 1 + ext};
    case 5: return {x + 2 + ext, z, y + 3 + ext};
  }
  return {};
}

static unordered_set<uint32_t> assemblyTargetSet(int length) {
  unordered_set<uint32_t> target;
  for (int slot = 0; slot < 6; ++slot) {
    for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < length; ++z) {
      Vec3 g = assemblySlotMap(slot, x, y, z, length);
      target.insert(flatIdx(g.x, g.y, g.z, length));
    }
  }
  return target;
}

static unordered_set<uint32_t> shiftedBaseTargetSet(int length) {
  int ext = extLen(length);
  unordered_set<uint32_t> out;
  for (uint32_t k : assemblyTargetSet(BASE_LEN)) {
    int x = k % BASE_LEN;
    int y = (k / BASE_LEN) % BASE_LEN;
    int z = k / (BASE_LEN * BASE_LEN);
    out.insert(flatIdx(x + ext, y + ext, z + ext, length));
  }
  return out;
}

static Target makeStandardTarget(int length) {
  Target t;
  t.name = "完整目标锁形 2x2x" + to_string(length);
  t.length = length;
  t.allowed = assemblyTargetSet(length);
  t.required = t.allowed;
  return t;
}

static Target makeHollowTarget(int length) {
  Target t;
  t.name = "空心避让目标锁形 2x2x" + to_string(length);
  t.length = length;
  int ext = extLen(length);
  for (int i = 0; i < (int)HOLLOW_TARGET_SHAPE.size(); ++i) {
    char ch = HOLLOW_TARGET_SHAPE[i];
    if (ch != '#' && ch != '+') continue;
    int x = i % 6;
    int y = (i / 6) % 6;
    int z = i / 36;
    uint32_t k = flatIdx(x + ext, y + ext, z + ext, length);
    t.allowed.insert(k);
    if (ch == '#') t.required.insert(k);
  }
  if (length > BASE_LEN) {
    auto fullLong = assemblyTargetSet(length);
    auto shiftedCore = shiftedBaseTargetSet(length);
    for (uint32_t k : fullLong) {
      if (shiftedCore.count(k)) continue;
      t.allowed.insert(k);
      t.required.insert(k);
    }
  }
  return t;
}

static Target selectTarget(int solidCount, int length) {
  Target standard = makeStandardTarget(length);
  if (solidCount == (int)standard.required.size()) return standard;
  return makeHollowTarget(length);
}

static vector<Vec3> pillarCodeToLocalVoxels(int code, int length) {
  int ext = extLen(length);
  uint64_t holeMask = 0;
  for (int p = 0; p < 12; ++p) {
    if (code & (1 << p)) holeMask |= 1ULL << (IP[p][0] + 2*IP[p][1] + 4*(IP[p][2] + ext));
  }
  vector<Vec3> cells;
  for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < length; ++z) {
    if (!(holeMask & (1ULL << (x + 2*y + 4*z)))) cells.push_back({x, y, z});
  }
  return cells;
}

static vector<array<int, 9>> buildRodRotations(int length) {
  vector<array<int, 3>> full;
  for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < length; ++z) full.push_back({x, y, z});
  vector<array<int, 9>> rots;
  set<string> seen;
  for (const auto &m : NR) {
    int minX = 1000000, minY = 1000000, minZ = 1000000;
    vector<array<int, 3>> raw;
    for (auto p : full) {
      int nx = m[0]*p[0] + m[1]*p[1] + m[2]*p[2];
      int ny = m[3]*p[0] + m[4]*p[1] + m[5]*p[2];
      int nz = m[6]*p[0] + m[7]*p[1] + m[8]*p[2];
      minX = min(minX, nx); minY = min(minY, ny); minZ = min(minZ, nz);
      raw.push_back({nx, ny, nz});
    }
    bool ok = true;
    vector<array<int, 3>> moved;
    for (auto p : raw) {
      int x = p[0] - minX, y = p[1] - minY, z = p[2] - minZ;
      if (x < 0 || x >= 2 || y < 0 || y >= 2 || z < 0 || z >= length) ok = false;
      moved.push_back({x, y, z});
    }
    if (!ok) continue;
    vector<string> parts;
    for (auto p : moved) parts.push_back(key3(p[0], p[1], p[2]));
    sort(parts.begin(), parts.end());
    string sig;
    for (auto &s : parts) sig += s + "|";
    sig += ":";
    for (int v : m) sig += to_string(v) + ",";
    if (seen.insert(sig).second) rots.push_back(m);
  }
  return rots;
}

static Vec3 rotateLocalCell(const Vec3 &v, const array<int, 9> &m, int length) {
  struct Raw { int x, y, z, nx, ny, nz; };
  vector<Raw> raw;
  int minX = 1000000, minY = 1000000, minZ = 1000000;
  for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < length; ++z) {
    int nx = m[0]*x + m[1]*y + m[2]*z;
    int ny = m[3]*x + m[4]*y + m[5]*z;
    int nz = m[6]*x + m[7]*y + m[8]*z;
    minX = min(minX, nx); minY = min(minY, ny); minZ = min(minZ, nz);
    raw.push_back({x, y, z, nx, ny, nz});
  }
  for (const auto &p : raw) {
    if (p.x == v.x && p.y == v.y && p.z == v.z) return {p.nx - minX, p.ny - minY, p.nz - minZ};
  }
  return {};
}

static bool assemblyCandidateForSlot(const vector<Vec3> &local, int code, int pieceIndex, int slot, int rotIndex,
                                     const Target &target, int length, const vector<array<int, 9>> &rotations,
                                     PiecePlacement &out) {
  const auto &m = rotations[rotIndex];
  if (det3(m) != assemblySlotDet(slot)) return false;
  vector<uint32_t> used;
  out = PiecePlacement{};
  out.code = code;
  out.piece = pieceIndex + 1;
  out.slot = slot;
  out.rotIndex = rotIndex;
  for (const Vec3 &v : local) {
    Vec3 r = rotateLocalCell(v, m, length);
    Vec3 g = assemblySlotMap(slot, r.x, r.y, r.z, length);
    uint32_t k = flatIdx(g.x, g.y, g.z, length);
    if (!target.allowed.count(k)) return false;
    for (uint32_t u : used) if (u == k) return false;
    used.push_back(k);
    out.cells.push_back(g);
    out.keys.push_back(k);
  }
  return true;
}

// ============================================================================
// 预处理优化 (移植自小程序/网页版 burrAlgorithm.js)
//   - 自对称性检测 (calculateSelfSymmetries): 遍历 24 种纯旋转, 统计使形状不变的旋转
//   - 自镜像检测 (isSelfMirroring): 是否存在镜像旋转使形状不变
//   - 镜像配对 (findMirrorPartner): 两根柱子是否互为镜像
//   - 改造 findAssemblyFromCodes 的候选构建:
//       1) 选对称破坏者 (用得最少的旋转)
//       2) 镜像配对里只探索一根
//       3) 缓存相同形状的 byPiece (用规范形状 key 作缓存 key)
// ============================================================================

// 对局部体素直接做线性变换并平移到原点 (与网页版/小程序 transformLocal 一致:
// 使用体素子集自身的包围盒做平移, 保证规范形式与 JS 版完全一致, 从而对称/镜像判定对齐)。
static vector<Vec3> transformLocal(const vector<Vec3> &local, const array<int, 9> &m, int /*length*/) {
  vector<Vec3> out(local.size());
  int minX = 1000000, minY = 1000000, minZ = 1000000;
  for (size_t i = 0; i < local.size(); ++i) {
    int nx = m[0]*local[i].x + m[1]*local[i].y + m[2]*local[i].z;
    int ny = m[3]*local[i].x + m[4]*local[i].y + m[5]*local[i].z;
    int nz = m[6]*local[i].x + m[7]*local[i].y + m[8]*local[i].z;
    if (nx < minX) minX = nx;
    if (ny < minY) minY = ny;
    if (nz < minZ) minZ = nz;
    out[i] = {nx, ny, nz};
  }
  for (auto &v : out) { v.x -= minX; v.y -= minY; v.z -= minZ; }
  return out;
}

static string localKey(const vector<Vec3> &local) {
  vector<string> parts;
  parts.reserve(local.size());
  for (const auto &v : local) parts.push_back(key3(v.x, v.y, v.z));
  sort(parts.begin(), parts.end());
  string s;
  for (auto &p : parts) s += p + ";";
  return s;
}

// 规范化: 取字典序最小的旋转形式 (cc-equal 编码 → 相同 canonical)
static void canonicalizeLocalCells(const vector<Vec3> &local, const vector<array<int, 9>> &rots, int length,
                                   vector<Vec3> &outCells, string &outKey) {
  outCells.clear();
  outKey.clear();
  if (local.empty()) return;
  string bestKey;
  for (size_t t = 0; t < rots.size(); ++t) {
    vector<Vec3> tr = transformLocal(local, rots[t], length);
    string k = localKey(tr);
    if (bestKey.empty() || k < bestKey) { bestKey = k; outCells = std::move(tr); }
  }
  outKey = bestKey;
}

// 1) 遍历 24 种纯旋转, 统计使形状不变的旋转索引 → 返回 set (含 0 度旋转)
static set<int> calculateSelfSymmetries(const vector<Vec3> &local, const vector<array<int, 9>> &rot24, int length) {
  set<int> syms;
  syms.insert(0);
  if (local.empty()) return syms;
  vector<Vec3> canon;
  string canonKey;
  canonicalizeLocalCells(local, rot24, length, canon, canonKey);
  for (size_t t = 1; t < rot24.size(); ++t) {
    if (localKey(transformLocal(local, rot24[t], length)) == canonKey) syms.insert((int)t);
  }
  return syms;
}

// 2) 检查是否存在镜像旋转使形状不变
static bool isSelfMirroring(const vector<Vec3> &local, const vector<array<int, 9>> &rot48, int length) {
  if (local.empty()) return true;
  vector<Vec3> canon;
  string canonKey;
  canonicalizeLocalCells(local, vector<array<int, 9>>(rot48.begin(), rot48.begin() + 24), length, canon, canonKey);
  for (size_t t = 24; t < rot48.size(); ++t) {
    if (localKey(transformLocal(local, rot48[t], length)) == canonKey) return true;
  }
  return false;
}

// 3) 检查两根柱子是否互为镜像 (排除纯旋转相同的情况)
static bool findMirrorPartner(const vector<Vec3> &localA, const vector<Vec3> &localB,
                              const vector<array<int, 9>> &rot48, int length) {
  if (localA.size() != localB.size()) return false;
  vector<array<int, 9>> rot24(rot48.begin(), rot48.begin() + 24);
  vector<Vec3> canonA, canonB;
  string keyA, keyB;
  canonicalizeLocalCells(localA, rot24, length, canonA, keyA);
  canonicalizeLocalCells(localB, rot24, length, canonB, keyB);
  if (keyA == keyB) return false; // 完全相同, 不是镜像
  for (size_t t = 24; t < rot48.size(); ++t) {
    if (localKey(transformLocal(canonA, rot48[t], length)) == keyB) return true;
  }
  return false;
}

// byPiece 缓存: 同一规范形状只计算一次
static unordered_map<string, vector<PiecePlacement>> &byPieceCache() {
  static unordered_map<string, vector<PiecePlacement>> cache;
  return cache;
}

static const vector<array<int, 9>> &ROT24() {
  static vector<array<int, 9>> r;
  if (r.empty()) {
    const auto &all = NR;
    r.assign(all.begin(), all.begin() + 24); // 前 24 个为纯旋转 (det3 == 1)
  }
  return r;
}

static const vector<array<int, 9>> &ROT48() {
  return NR; // NR 含 48 个 (前 24 纯旋转, 后 24 镜像)
}

// 用规范形状构建候选摆放 (带缓存)
static vector<PiecePlacement> buildByPieceForCanonical(const vector<Vec3> &canonicalCells, int length,
                                                        const Target &target, const vector<array<int, 9>> &rotations) {
  string cacheKey = localKey(canonicalCells) + "|" + to_string(length) + "|" + target.name;
  auto &cache = byPieceCache();
  auto it = cache.find(cacheKey);
  if (it != cache.end()) return it->second;

  vector<PiecePlacement> list;
  set<string> seen;
  for (int slot = 0; slot < 6; ++slot) {
    for (size_t r = 0; r < rotations.size(); ++r) {
      PiecePlacement cand;
      if (!assemblyCandidateForSlot(canonicalCells, -1, -1, slot, (int)r, target, length, rotations, cand)) continue;
      vector<uint32_t> sortedKeys = cand.keys;
      sort(sortedKeys.begin(), sortedKeys.end());
      string sig = to_string(cand.slot) + ":";
      for (auto k : sortedKeys) sig += to_string(k) + "|";
      if (!seen.insert(sig).second) continue;
      list.push_back(cand);
    }
  }
  cache[cacheKey] = list;
  return list;
}

// 从一组柱子摆放中提取目标锁形（用于旋转前锁形重新拼装）
static Target makeTargetFromPieces(const vector<PiecePlacement> &pieces, const string &name, int length) {
  Target t;
  t.name = name;
  t.length = length;
  for (auto &p : pieces) {
    for (auto &c : p.cells) {
      uint32_t k = flatIdx(c.x, c.y, c.z, length);
      t.required.insert(k);
      t.allowed.insert(k);
    }
  }
  return t;
}

// 使用自定义目标锁形构建候选摆放（用于旋转前锁形重新拼装）
static vector<vector<PiecePlacement>> buildCandidatesWithTarget(const vector<int> &codes, int length, const Target &target) {
  vector<array<int, 9>> rotations = buildRodRotations(length);
  vector<vector<Vec3>> locals;
  for (int code : codes) locals.push_back(pillarCodeToLocalVoxels(code, length));
  vector<vector<PiecePlacement>> byPiece(codes.size());
  for (int i = 0; i < (int)codes.size(); ++i) {
    set<string> seen;
    for (int slot = 0; slot < 6; ++slot) {
      for (int r = 0; r < (int)rotations.size(); ++r) {
        PiecePlacement cand;
        if (!assemblyCandidateForSlot(locals[i], codes[i], i, slot, r, target, length, rotations, cand)) continue;
        vector<uint32_t> sortedKeys = cand.keys;
        sort(sortedKeys.begin(), sortedKeys.end());
        string sig = to_string(cand.slot) + ":";
        for (auto k : sortedKeys) sig += to_string(k) + "|";
        if (!seen.insert(sig).second) continue;
        byPiece[i].push_back(cand);
      }
    }
  }
  return byPiece;
}

static State buildState(const vector<PiecePlacement> &pieces) {
  State st;
  for (int i = 0; i < (int)pieces.size(); ++i) {
    int id = i + 1;
    st.pieces.push_back(id);
    st.vox[id] = pieces[i].cells;
  }
  return st;
}

static string sourceSignature(const State &st) {
  int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
  for (int p : st.pieces) for (auto v : st.vox.at(p)) {
    minX = min(minX, v.x); minY = min(minY, v.y); minZ = min(minZ, v.z);
  }
  string sig;
  for (int p : st.pieces) sig += to_string(p) + ",";
  sig += "|";
  for (int p : st.pieces) {
    vector<string> cells;
    for (auto v : st.vox.at(p)) cells.push_back(key3(v.x - minX, v.y - minY, v.z - minZ));
    sort(cells.begin(), cells.end());
    sig += to_string(p) + ":";
    for (auto &c : cells) sig += c + ";";
    sig += "|";
  }
  return sig;
}

static string candSig(const vector<PiecePlacement> &c, int length) {
  string s;
  for (const auto &p : c) {
    s += "P" + to_string(p.piece) + ":";
    vector<uint32_t> cs;
    for (const auto &v : p.cells) cs.push_back(flatIdx(v.x, v.y, v.z, length));
    sort(cs.begin(), cs.end());
    for (auto x : cs) s += to_string(x) + ";";
    s += "|";
  }
  return s;
}

static int gapBetweenPieces(const State &st, int movingPiece, int fixedPiece, const Dir &dir) {
  int gap = BURR_REMOVE_DISTANCE;
  const auto &aCells = st.vox.at(movingPiece);
  const auto &bCells = st.vox.at(fixedPiece);
  for (auto a : aCells) for (auto b : bCells) {
    if (dir.dx) {
      if (a.y != b.y || a.z != b.z) continue;
      int d = dir.dx > 0 ? b.x - a.x - 1 : a.x - b.x - 1;
      if (d >= 0 && d < gap) gap = d;
    } else if (dir.dy) {
      if (a.x != b.x || a.z != b.z) continue;
      int d = dir.dy > 0 ? b.y - a.y - 1 : a.y - b.y - 1;
      if (d >= 0 && d < gap) gap = d;
    } else {
      if (a.x != b.x || a.y != b.y) continue;
      int d = dir.dz > 0 ? b.z - a.z - 1 : a.z - b.z - 1;
      if (d >= 0 && d < gap) gap = d;
    }
  }
  return gap;
}

static vector<int> movementClosure(const State &st, int startPiece, const Dir &dir, int amount, int maxPieces) {
  uint8_t movingMask = 0;
  int movingCount = 0;
  int pieceIdx[7] = {};
  for (int i = 0; i < (int)st.pieces.size(); i++) pieceIdx[st.pieces[i]] = i;
  movingMask |= 1 << pieceIdx[startPiece];
  movingCount = 1;
  bool changed = true;
  while (changed) {
    changed = false;
    for (int pi = 0; pi < (int)st.pieces.size(); pi++) {
      if (!(movingMask & (1 << pi))) continue;
      int p = st.pieces[pi];
      for (int qi = 0; qi < (int)st.pieces.size(); qi++) {
        if (pi == qi || (movingMask & (1 << qi))) continue;
        int q = st.pieces[qi];
        if (amount > gapBetweenPieces(st, p, q, dir)) {
          movingMask |= 1 << qi;
          movingCount++;
          if (movingCount > maxPieces || movingCount >= (int)st.pieces.size()) return {};
          changed = true;
        }
      }
    }
  }
  vector<int> result;
  for (int i = 0; i < (int)st.pieces.size(); i++)
    if (movingMask & (1 << i)) result.push_back(st.pieces[i]);
  return result;
}

static State moveStep(const State &st, const vector<int> &group, const Dir &dir) {
  uint8_t mask = 0;
  int idxMap[7] = {};
  for (int i = 0; i < (int)st.pieces.size(); i++) idxMap[st.pieces[i]] = i;
  for (int p : group) mask |= 1 << idxMap[p];
  State next;
  next.pieces = st.pieces;
  for (int i = 0; i < (int)st.pieces.size(); i++) {
    int p = st.pieces[i];
    for (auto v : st.vox.at(p)) {
      if (mask & (1 << i)) v = {v.x + dir.dx, v.y + dir.dy, v.z + dir.dz};
      next.vox[p].push_back(v);
    }
  }
  return next;
}

static State pickState(const State &st, const vector<int> &pieces) {
  uint8_t mask = 0;
  int idxMap[7] = {};
  for (int i = 0; i < (int)st.pieces.size(); i++) idxMap[st.pieces[i]] = i;
  for (int p : pieces) mask |= 1 << idxMap[p];
  State next;
  for (int i = 0; i < (int)st.pieces.size(); i++) {
    int p = st.pieces[i];
    if (mask & (1 << i)) {
      next.pieces.push_back(p);
      next.vox[p] = st.vox.at(p);
    }
  }
  return next;
}

static State nextStateWithoutGroup(const State &st, const vector<int> &group) {
  uint8_t mask = 0;
  int idxMap[7] = {};
  for (int i = 0; i < (int)st.pieces.size(); i++) idxMap[st.pieces[i]] = i;
  for (int p : group) mask |= 1 << idxMap[p];
  State next;
  for (int i = 0; i < (int)st.pieces.size(); i++) {
    int p = st.pieces[i];
    if (!(mask & (1 << i))) {
      next.pieces.push_back(p);
      next.vox[p] = st.vox.at(p);
    }
  }
  return next;
}

static vector<Action> sourceMoves(const State &st) {
  vector<Action> moves;
  vector<uint64_t> seen;
  int maxGroup = (int)st.pieces.size() / 2;
  auto addMove = [&](const vector<int> &group, const Dir &dir, int steps, bool exit) {
    if (group.empty()) return;
    uint64_t key = 0;
    for (int p : group) key |= 1ULL << p;
    int dirIdx = (int)(&dir - &BURR_DIRS[0]);
    key |= (uint64_t)dirIdx << 32;
    key |= (uint64_t)steps << 40;
    key |= (uint64_t)(exit ? 1 : 0) << 48;
    for (uint64_t s : seen) if (s == key) return;
    seen.push_back(key);
    moves.push_back({group, dir, steps, exit, false, false, {}, 0});
  };
  for (const Dir &dir : BURR_DIRS) for (int p : st.pieces) addMove(movementClosure(st, p, dir, BURR_REMOVE_DISTANCE, 1), dir, 8, true);
  for (const Dir &dir : BURR_DIRS) for (int p : st.pieces) addMove(movementClosure(st, p, dir, BURR_REMOVE_DISTANCE, maxGroup), dir, 8, true);
  for (const Dir &dir : BURR_DIRS) for (int p : st.pieces) addMove(movementClosure(st, p, dir, 1, maxGroup), dir, 1, false);
  return moves;
}

struct Node {
  State st;
  int parent = -1;
  Action action;
  bool hasAction = false;
};

static vector<Action> tracePath(const vector<Node> &nodes, int idx) {
  vector<Action> path;
  while (idx >= 0 && nodes[idx].hasAction) {
    path.push_back(nodes[idx].action);
    idx = nodes[idx].parent;
  }
  reverse(path.begin(), path.end());
  return path;
}

static vector<Action> compactPlan(const vector<Action> &plan) {
  vector<Action> out;
  for (auto action : plan) {
    if (!out.empty()) {
      Action &last = out.back();
      if (!last.exit && !last.rotate && !action.rotate && string(last.dir.name) == action.dir.name && last.group == action.group) {
        last.steps += action.steps;
        if (action.exit) {
          last.exit = true;
          last.finalRemoval = action.finalRemoval;
        }
        continue;
      }
    }
    out.push_back(action);
  }
  return out;
}

static bool disassembleRecBfs(const State &st, int &nodeCount, vector<Action> &outPlan,
                              const chrono::steady_clock::time_point &deadline) {
  if (st.pieces.size() <= 1) { outPlan.clear(); return true; }
  if (chrono::steady_clock::now() >= deadline) return false;
  vector<Node> nodes;
  nodes.push_back({st, -1, {}, false});
  queue<int> q; q.push(0);
  unordered_set<uint64_t> closed;
  closed.insert(fastStateSig(st));

  while (!q.empty()) {
    if ((nodeCount & 31) == 0 && chrono::steady_clock::now() >= deadline) return false;
    int idx = q.front(); q.pop();
    if (++nodeCount > BURR_STATE_LIMIT) return false;
    // 注意：必须用值拷贝而非引用。下方 nodes.push_back 可能使 vector 重新分配，
    // 若 cur 为引用则会变成悬空引用，在 -O2 严格别名分析下导致 UB（拆解搜索误判）。
    State cur = nodes[idx].st;
    for (Action action : sourceMoves(cur)) {
      if (action.exit) {
        State afterSplit = moveStep(cur, action.group, action.dir);
        State separated = pickState(afterSplit, action.group);
        State rest = nextStateWithoutGroup(cur, action.group);
        vector<Action> separatedPlan, restPlan;
        if (!disassembleRecBfs(separated, nodeCount, separatedPlan, deadline)) continue;
        if (!disassembleRecBfs(rest, nodeCount, restPlan, deadline)) continue;
        action.finalRemoval = action.group.size() == 1;
        outPlan = tracePath(nodes, idx);
        outPlan.push_back(action);
        outPlan.insert(outPlan.end(), separatedPlan.begin(), separatedPlan.end());
        outPlan.insert(outPlan.end(), restPlan.begin(), restPlan.end());
        outPlan = compactPlan(outPlan);
        return true;
      }
      State moved = moveStep(cur, action.group, action.dir);
      uint64_t h = fastStateSig(moved);
      if (closed.count(h)) continue;
      closed.insert(h);
      nodes.push_back({moved, idx, action, true});
      q.push((int)nodes.size() - 1);
    }
  }
  return false;
}

static bool findDisassembly(const vector<PiecePlacement> &pieces, vector<Action> &plan) {
  try {
    int nodeCount = 0;
    State st = buildState(pieces);
    auto deadline = chrono::steady_clock::now() + chrono::milliseconds(gDisasmTimeoutMs);
    return disassembleRecBfs(st, nodeCount, plan, deadline);
  } catch (const bad_alloc &) {
    plan.clear();
    return false;
  }
}

struct Stage {
  int start = 0;
  int end = 0;
  Action exitAction;
  int moveCount = 0;
};

static vector<Stage> makeStages(const vector<Action> &plan) {
  vector<Stage> stages;
  int start = 0;
  for (int i = 0; i < (int)plan.size(); ++i) {
    if (plan[i].rotate) {
      stages.push_back({i, i, plan[i], 1});
      start = i + 1;
      continue;
    }
    if (!plan[i].exit) continue;
    stages.push_back({start, i, plan[i], i - start + 1});
    start = i + 1;
  }
  if (stages.empty() && !plan.empty()) stages.push_back({0, (int)plan.size() - 1, plan.back(), (int)plan.size()});
  return stages;
}

static Difficulty planDifficulty(const vector<Action> &plan) {
  vector<Stage> stages = makeStages(plan);
  Difficulty d;
  d.firstLevel = stages.empty() ? (int)plan.size() : stages[0].moveCount;
  d.exitCount = (int)stages.size();
  d.totalMoves = (int)plan.size();
  for (auto &s : stages) d.maxStage = max(d.maxStage, s.moveCount);
  d.score = 1LL * d.firstLevel * 1000000 + 1LL * d.maxStage * 10000 + d.totalMoves;
  return d;
}

static string actionText(const Action &a) {
  if (a.rotate) {
    string axisStr(1, toupper(a.rotationAxis));
    return "旋转解锁：柱子 " + to_string(a.group[0]) + " 绕方块 " + key3(a.pivot.x, a.pivot.y, a.pivot.z) +
      " 的轴心，沿" + axisStr + "轴旋转 " + (a.rotationDir > 0 ? axisStr + "+90" : axisStr + "-90");
  }
  string verb = a.exit ? (a.finalRemoval ? "移除" : "分离") : "调整";
  string group;
  for (size_t i = 0; i < a.group.size(); ++i) {
    if (i) group += "+";
    group += to_string(a.group[i]);
  }
  return verb + "柱子 " + group + "：向 " + a.dir.name + " 滑动 " + to_string(a.steps) + " 格";
}

static Vec3 rotateYCellAroundPivot(const Vec3 &cell, const Vec3 &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int nx = dir > 0 ? z : -z;
  int nz = dir > 0 ? -x : x;
  return {nx + pivot.x, y + pivot.y, nz + pivot.z};
}

static Vec3 rotateXCellAroundPivot(const Vec3 &cell, const Vec3 &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int ny = dir > 0 ? -z : z;
  int nz = dir > 0 ? y : -y;
  return {x + pivot.x, ny + pivot.y, nz + pivot.z};
}

static Vec3 rotateZCellAroundPivot(const Vec3 &cell, const Vec3 &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int nx = dir > 0 ? -y : y;
  int ny = dir > 0 ? x : -x;
  return {nx + pivot.x, ny + pivot.y, z + pivot.z};
}

static bool validatePieces(const vector<PiecePlacement> &pieces, int length) {
  vector<bool> occ(length * length * length, false);
  for (auto &p : pieces) for (auto c : p.cells) {
    if (c.x < 0 || c.x >= length || c.y < 0 || c.y >= length || c.z < 0 || c.z >= length) return false;
    int k = flatIdx(c.x, c.y, c.z, length);
    if (occ[k]) return false;
    occ[k] = true;
  }
  return true;
}

// 前向声明（被 findAssemblyFromCodes 使用）—— 实现在文件末尾附近
static bool findRotationUnlockedSolution(const vector<int> &codes, const vector<PiecePlacement> &lockedPieces, int length, VerifyResult &vr);

struct AssemblyResult {
  bool ok = false;
  bool error = false;
  bool deadlocked = false;
  bool searchLimited = false;
  string errorText;
  vector<PiecePlacement> pieces;
  vector<vector<PiecePlacement>> candidates;
  Target target;
  VerifyResult verify;
  int nodes = 0;
  int checkedAssemblies = 0;
  int skippedDuplicates = 0;          // 整体对称去重跳过的等价装配数
  set<string> verifiedSigs;           // 已验证装配的 canonical 签名（不论拆解成败）
  int disasmSolutionCountNoRot = 0;  // 枚举模式下：非旋转可拆装配方案数（含朝向）
  int disasmSolutionCountRot = 0;   // 枚举模式下：旋转可拆装配方案数（含朝向）
  set<string> disasmSigsNoRot;       // 枚举模式下：去重的非旋转装配签名
  set<string> disasmSigsRot;         // 枚举模式下：去重的旋转装配签名
  vector<string> disasmAsmsNoRot;    // 枚举模式下：非旋转可拆装配列表（格式化字符串）
  vector<string> disasmAsmsRot;      // 枚举模式下：旋转可拆装配列表
};

// 生成全部 48 种 O_h 立方对称变换：6 种轴置换 × 8 种符号组合
static const vector<pair<array<int,3>, array<int,3>>>& cubeSymmetries() {
  static vector<pair<array<int,3>, array<int,3>>> syms;
  if (!syms.empty()) return syms;
  int axes[3] = {0, 1, 2};
  do {
    array<int,3> perm = {axes[0], axes[1], axes[2]};
    for (int sx : {-1, 1})
      for (int sy : {-1, 1})
        for (int sz : {-1, 1}) {
          array<int,3> signs = {sx, sy, sz};
          syms.push_back({perm, signs});
        }
  } while (next_permutation(axes, axes + 3));
  return syms;
}

// 计算装配的 O_h 立方对称 canonical 签名（6柱所有全局坐标体素，经48种变换取最小字典序）
static string canonicalAssemblySig(const vector<PiecePlacement>& pieces, int length) {
  const auto& syms = cubeSymmetries();
  string best;
  for (const auto& sym : syms) {
    const auto& perm = sym.first;
    const auto& signs = sym.second;
    int minX = 1<<29, minY = 1<<29, minZ = 1<<29;
    vector<vector<Vec3>> tc(6);
    vector<int> pcode(6);
    for (int i = 0; i < 6; ++i) {
      pcode[i] = pieces[i].code;
      for (const Vec3& c : pieces[i].cells) {
        int old[3] = {c.x, c.y, c.z};
        Vec3 nc = {signs[0]*old[perm[0]], signs[1]*old[perm[1]], signs[2]*old[perm[2]]};
        minX = min(minX, nc.x); minY = min(minY, nc.y); minZ = min(minZ, nc.z);
        tc[i].push_back(nc);
      }
    }
    for (auto& piece : tc)
      for (auto& c : piece) { c.x -= minX; c.y -= minY; c.z -= minZ; }
    vector<string> psigs;
    for (int i = 0; i < 6; ++i) {
      vector<int> cs;
      for (const Vec3& c : tc[i]) cs.push_back(flatIdx(c.x, c.y, c.z, length));
      sort(cs.begin(), cs.end());
      string ps = to_string(pcode[i]) + "|";
      for (int x : cs) ps += to_string(x) + ";";
      psigs.push_back(ps);
    }
    sort(psigs.begin(), psigs.end());
    string sig;
    for (const auto& ps : psigs) sig += ps + "||";
    if (best.empty() || sig < best) best = sig;
  }
  return best;
}

// 槽位编号 → 方位名称
static const char* slotDirName(int slot) {
  static const char* names[] = {"左", "右", "前", "后", "下", "上"};
  return (slot >= 0 && slot < 6) ? names[slot] : "?";
}

// 格式化装配为 "编码方位,编码方位,..." （按槽位排序）
static string formatAssembly(const vector<PiecePlacement>& pieces) {
  vector<PiecePlacement> sorted = pieces;
  sort(sorted.begin(), sorted.end(), [](const PiecePlacement& a, const PiecePlacement& b) {
    return a.slot < b.slot;
  });
  string s;
  for (size_t i = 0; i < sorted.size(); ++i) {
    if (i) s += ",";
    s += to_string(sorted[i].code) + slotDirName(sorted[i].slot);
  }
  return s;
}

static AssemblyResult findAssemblyFromCodes(const vector<int> &codes, int length,
                                            bool enumerateAll = false,
                                            bool noRotation = false,
                                            bool rotOnce = false,
                                            int candidateCap = -1,
                                            const Target* customTarget = nullptr) {
  vector<array<int, 9>> rotations = buildRodRotations(length);
  vector<vector<Vec3>> locals;
  int solidCount = 0;
  for (int code : codes) {
    locals.push_back(pillarCodeToLocalVoxels(code, length));
    solidCount += (int)locals.back().size();
  }
  Target target = customTarget ? *customTarget : selectTarget(solidCount, length);

  vector<vector<PiecePlacement>> byPiece;
  if (customTarget) {
    byPiece = buildCandidatesWithTarget(codes, length, target);
    for (int i = 0; i < (int)byPiece.size(); ++i) {
      if (byPiece[i].empty()) {
        AssemblyResult ar;
        ar.error = true;
        ar.errorText = "旋转前锁形：第 " + to_string(i + 1) + " 根柱子没有可用摆放。";
        ar.target = target;
        return ar;
      }
    }
  } else {
    // ---- 预处理优化: 规范化 + 自对称 + 镜像配对 + byPiece 缓存 ----
    const auto &rot24 = ROT24();
    const auto &rot48 = ROT48();

    // 0) 规范形式 (cc-equal 编码 → 相同 canonical, 保证 byPiece 一致)
    vector<vector<Vec3>> canonicals(codes.size());
    vector<string> canonKeys(codes.size());
    for (size_t i = 0; i < codes.size(); ++i) {
      canonicalizeLocalCells(locals[i], rot24, length, canonicals[i], canonKeys[i]);
    }

    // 1) 自对称性: 每根柱子的对称旋转集; 同时标记自镜像柱 (isSelfMirroring)
    vector<set<int>> selfSym(codes.size());
    vector<bool> selfMirror(codes.size(), false);
    for (size_t i = 0; i < codes.size(); ++i) {
      selfSym[i] = calculateSelfSymmetries(locals[i], rot24, length);
      selfMirror[i] = isSelfMirroring(locals[i], rot48, length);
    }
    (void)selfMirror;

    // 2) 选"对称破坏者": 在所有柱子的自对称并集中选被使用次数最少的旋转
    //    优先选不在任何柱子对称集中的旋转 (出现次数=0), 其次出现次数最少
    vector<int> useCount(24, 0);
    for (const auto &s : selfSym) for (int t : s) if (t < 24) useCount[t]++;
    int breaker = 0, bestScore = 1000000;
    for (int t = 0; t < 24; ++t) {
      int score = (useCount[t] == 0 ? 0 : 1000) + useCount[t];
      if (score < bestScore) { bestScore = score; breaker = t; }
    }

    // 3) 镜像配对检测: 标记互为镜像的柱子 (findMirrorPartner)。
    //    注意: 网页版/小程序中 byPiece 仍按规范形状独立生成 (靠 canonical 缓存
    //    自然去重相同形状), 不直接克隆根的 byPiece —— 因为镜像体素的手性不同,
    //    直接克隆会导致伙伴柱子用到错误的摆放而拼装失败。此处仅记录配对信息。
    vector<int> mirrorPairs(codes.size(), -1);
    for (size_t i = 0; i < codes.size(); ++i) {
      for (size_t j = i + 1; j < codes.size(); ++j) {
        if (findMirrorPartner(locals[i], locals[j], rot48, length)) {
          mirrorPairs[i] = (int)j;
          mirrorPairs[j] = (int)i;
        }
      }
    }

    // 4) 生成 byPiece: 按规范形状缓存 (相同 canonical → 只算一次)
    byPiece.resize(codes.size());
    for (size_t i = 0; i < codes.size(); ++i) {
      vector<PiecePlacement> list = buildByPieceForCanonical(canonicals[i], length, target, rotations);
      if (list.empty()) {
        AssemblyResult ar;
        ar.error = true;
        ar.errorText = "第 " + to_string(i + 1) + " 根柱子没有可用摆放。";
        ar.target = target;
        return ar;
      }
      byPiece[i].clear();
      byPiece[i].reserve(list.size());
      for (auto &c : list) {
        PiecePlacement reb = c;
        reb.code = codes[i];
        reb.piece = (int)i + 1;
        byPiece[i].push_back(reb);
      }
    }

    (void)breaker;
    (void)mirrorPairs;
  }

  vector<int> order(codes.size());
  for (int i = 0; i < (int)codes.size(); ++i) order[i] = i;
  stable_sort(order.begin(), order.end(), [&](int a, int b) { return byPiece[a].size() < byPiece[b].size(); });

  int gridCells = length * length * length;
  vector<bool> occ(gridCells, false);
  int occCount = 0;
  set<int> usedSlots;
  vector<PiecePlacement> placed(codes.size());
  vector<PiecePlacement> firstAssembly;
  int testedCandidates = 0;
  bool capReached = false;
  AssemblyResult result;
  result.target = target;
  auto enumT0 = chrono::steady_clock::now();

  static const int PHASE1_MAX_NODES = 20000;
  function<bool(int)> dfs = [&](int depth) -> bool {
    int nodeLimit = (candidateCap > 0) ? PHASE1_MAX_NODES : ASSEMBLY_MAX_NODES;
    if (++result.nodes > nodeLimit) {
      result.searchLimited = true;
      return true;
    }
    if (candidateCap < 0 && gEnumComboBudgetMs > 0 && (result.nodes & 1023) == 0 &&
        chrono::duration<double, milli>(chrono::steady_clock::now() - enumT0).count() > gEnumComboBudgetMs) {
      capReached = true;
      return true;
    }
    if (depth == (int)order.size()) {
      if (occCount != solidCount) return false;
      for (uint32_t k : target.required) if (!occ[k]) return false;
      if (candidateCap < 0 && gEnumComboBudgetMs > 0 &&
          chrono::duration<double, milli>(chrono::steady_clock::now() - enumT0).count() > gEnumComboBudgetMs) {
        capReached = true;
        return true;
      }
      vector<PiecePlacement> candidate = placed;
      ++result.checkedAssemblies;
      if (firstAssembly.empty()) firstAssembly = candidate;
      if (enumerateAll) result.candidates.push_back(candidate);

      string sig = canonicalAssemblySig(candidate, length);

      // 整体对称去重：跳过与已验证装配旋转/镜像等价的候选，避免重复拆解验证。
      // 同一等价类的装配拆解难度完全相同（旋转/镜像不改变内部咬合结构），只验证一个代表即可。
      if (!result.verifiedSigs.insert(sig).second) {
        ++result.skippedDuplicates;
        return false;
      }

      vector<Action> plan;
      bool disasmOk = findDisassembly(candidate, plan);
      bool found = disasmOk;
      if (disasmOk) {
        result.ok = true;
        if (result.disasmSigsNoRot.insert(sig).second) {
          result.disasmSolutionCountNoRot++;
          result.disasmAsmsNoRot.push_back(formatAssembly(candidate));
          if (result.pieces.empty()) result.pieces = candidate;
        }
        result.verify.ok = true;
        result.verify.pieces = candidate;
        result.verify.plan = plan;
        result.verify.difficulty = planDifficulty(plan);
        if (enumerateAll && candidateCap > 0) candidateCap = -1;
      }
      VerifyResult rot;
      if (!disasmOk && !noRotation && !rotOnce && findRotationUnlockedSolution(codes, candidate, length, rot)) {
        found = true;
        result.ok = true;
        if (result.disasmSigsRot.insert(sig).second) {
          result.disasmSolutionCountRot++;
          result.disasmAsmsRot.push_back(formatAssembly(rot.pieces));
          result.pieces = rot.pieces;
        }
        result.verify = rot;
      }
      if (enumerateAll) return false;
      if (found) return true;
      if (candidateCap > 0 && ++testedCandidates >= candidateCap) { capReached = true; return true; }
      return false;
    }
    int idx = order[depth];
    for (const PiecePlacement &cand : byPiece[idx]) {
      if (usedSlots.count(cand.slot)) continue;
      bool ok = true;
      for (uint32_t k : cand.keys) if (occ[k]) { ok = false; break; }
      if (!ok) continue;
      usedSlots.insert(cand.slot);
      for (uint32_t k : cand.keys) { occ[k] = true; occCount++; }
      placed[idx] = cand;
      if (dfs(depth + 1)) return true;
      for (uint32_t k : cand.keys) { occ[k] = false; occCount--; }
      usedSlots.erase(cand.slot);
    }
    return false;
  };

  dfs(0);
  if (!result.ok && rotOnce && !noRotation && !firstAssembly.empty()) {
    VerifyResult rot;
    if (findRotationUnlockedSolution(codes, firstAssembly, length, rot)) {
      result.ok = true;
      result.disasmSolutionCountRot = 1;
      result.disasmSigsRot.insert("rot-once");
      result.pieces = rot.pieces;
      result.verify = rot;
    }
  }
  if (!result.ok && !firstAssembly.empty()) {
    result.deadlocked = true;
    result.pieces = firstAssembly;
  } else if (!result.ok && !result.searchLimited) {
    result.error = true;
    result.errorText = "没有找到无碰撞且满足目标锁形的摆放。";
  } else if (!result.ok && result.searchLimited) {
    result.error = true;
    result.errorText = "装配搜索超过节点上限，已停止。";
  }
  return result;
}

static bool findRotationUnlockedSolution(const vector<int> &codes, const vector<PiecePlacement> &lockedPieces, int length, VerifyResult &vr) {
  struct RotFunc { char name; Vec3 (*fn)(const Vec3&, const Vec3&, int); };
  RotFunc funcs[] = {
    {'x', rotateXCellAroundPivot},
    {'y', rotateYCellAroundPivot},
    {'z', rotateZCellAroundPivot}
  };
  fprintf(stderr, "[旋转调试] 寻找旋转锁方案...\n");
  int solidCount = 0;
  for (int code : codes) {
    solidCount += (int)pillarCodeToLocalVoxels(code, length).size();
  }
  for (int idx = 0; idx < (int)codes.size(); ++idx) {
    const PiecePlacement &locked = lockedPieces[idx];
    // 1023 (0x3FF) 是标准旋转锁柱子（10个孔，实体数 = 2*2*length - 10）
    // 用形状匹配而不是编码匹配，确保等效编码也能找到旋转锁
    int targetCells = 2 * 2 * length - 10;
    if ((int)locked.cells.size() != targetCells) continue;
    fprintf(stderr, "[旋转调试] 柱子 %d (编码 %d): %zu 个格\n", idx + 1, codes[idx], locked.cells.size());
    for (auto &rf : funcs) {
      int validCount = 0, disasmCount = 0;
      fprintf(stderr, "[旋转调试]   轴 %c: ", rf.name);
      for (Vec3 pivot : locked.cells) {
        for (int dir : {1, -1}) {
          vector<PiecePlacement> unlocked = lockedPieces;
          unlocked[idx].cells.clear();
          unlocked[idx].keys.clear();
          for (Vec3 c : locked.cells) {
            Vec3 r = rf.fn(c, pivot, dir);
            unlocked[idx].cells.push_back(r);
            unlocked[idx].keys.push_back(flatIdx(r.x, r.y, r.z, length));
          }
          if (!validatePieces(unlocked, length)) continue;
          validCount++;
          vector<Action> plan;
          if (findDisassembly(unlocked, plan)) {
            disasmCount++;
            // 初始显示旋转前锁形，第一步是旋转解锁
            Action rotAction;
            rotAction.rotate = true;
            rotAction.group = {idx + 1};
            rotAction.pivot = pivot;
            rotAction.rotationDir = dir;
            rotAction.rotationAxis = rf.name;
            plan.insert(plan.begin(), rotAction);
            vr.ok = true;
            vr.rotatedShape = true;
            vr.pieces = lockedPieces;
            vr.plan = plan;
            vr.difficulty = planDifficulty(plan);
            vr.rotatePieceIndex = idx;
            vr.pivot = pivot;
            vr.unlockDir = dir;
            vr.rotateAxis = rf.name;
            return true;
          }

          // 情况2：旋转后直接拆解不了，用旋转前锁形重新搜索装配
          {
            Target preRotTarget = makeTargetFromPieces(unlocked, "旋转前锁形", length);
            if ((int)preRotTarget.required.size() == solidCount) {
              AssemblyResult rotAr = findAssemblyFromCodes(codes, length,
                /*enumerateAll=*/false, /*noRotation=*/true, /*rotOnce=*/false,
                /*candidateCap=*/-1, /*customTarget=*/&preRotTarget);
              if (rotAr.ok && !rotAr.pieces.empty()) {
                // 初始显示旋转前锁形，第一步是旋转解锁
                Action rotAction;
                rotAction.rotate = true;
                rotAction.group = {idx + 1};
                rotAction.pivot = pivot;
                rotAction.rotationDir = dir;
                rotAction.rotationAxis = rf.name;
                rotAr.verify.plan.insert(rotAr.verify.plan.begin(), rotAction);
                vr.ok = true;
                vr.rotatedShape = true;
                vr.pieces = lockedPieces;
                vr.plan = rotAr.verify.plan;
                vr.difficulty = rotAr.verify.difficulty;
                vr.rotatePieceIndex = idx;
                vr.pivot = pivot;
                vr.unlockDir = dir;
                vr.rotateAxis = rf.name;
                return true;
              }
            }
          }
        }
      }
      fprintf(stderr, "通过验证: %d/%zu, 可拆解: %d\n", validCount, locked.cells.size() * 2, disasmCount);
    }
  }
  fprintf(stderr, "[旋转调试] 未找到旋转锁方案\n");
  return false;
}

LubanSolveSummary solveLubanLock(const vector<int> &codes, int length) {
  LubanSolveSummary summary;
  summary.codes = codes;
  summary.length = length;

  auto t0 = chrono::steady_clock::now();
  auto finishTime = [&]() {
    auto t1 = chrono::steady_clock::now();
    summary.elapsedMs = chrono::duration<double, milli>(t1 - t0).count();
  };

  if (!(length == 6 || length == 8 || length == 10 || length == 12)) {
    summary.error = "柱长只支持 6、8、10、12。";
    finishTime();
    return summary;
  }
  if (codes.size() != 6) {
    summary.error = "需要输入 6 个柱子编码。";
    finishTime();
    return summary;
  }
  for (int c : codes) {
    if (c < 0 || c > 4095) {
      summary.error = "编码超出范围: " + to_string(c);
      finishTime();
      return summary;
    }
  }
  summary.inputOk = true;

  AssemblyResult ar = findAssemblyFromCodes(codes, length);
  finishTime();

  summary.targetName = ar.target.name;
  summary.requiredCells = (int)ar.target.required.size();
  summary.allowedCells = (int)ar.target.allowed.size();
  summary.searchNodes = ar.nodes;
  summary.checkedAssemblies = ar.checkedAssemblies;
  summary.skippedDuplicates = ar.skippedDuplicates;
  summary.searchLimited = ar.searchLimited;
  summary.deadlocked = ar.deadlocked;

  if (ar.error) {
    summary.error = ar.errorText;
    return summary;
  }

  summary.assemblyOk = true;

  // 验证结果已由 findAssemblyFromCodes 在搜索过程中完成
  if (ar.verify.ok) {
    vector<Action> postPlan;
    if (ar.verify.plan.empty()) {
      // 验证通过但未填 plan，这里重新跑 C++ BFS 获取拆解步骤
      findDisassembly(ar.pieces, postPlan);
      ar.verify.plan = postPlan;
    }
    summary.disassemblable = true;
    summary.rotationLock = ar.verify.rotatedShape;
    if (ar.verify.rotatedShape) {
      summary.rotationLock = true;
      summary.rotatePiece = ar.verify.rotatePieceIndex + 1;
      summary.pivotX = ar.verify.pivot.x;
      summary.pivotY = ar.verify.pivot.y;
      summary.pivotZ = ar.verify.pivot.z;
      summary.preRotation = std::string(1, toupper(ar.verify.rotateAxis)) + (ar.verify.unlockDir > 0 ? "-90" : "+90");
    }
    Difficulty dd = planDifficulty(ar.verify.plan);
    summary.firstLevel = dd.firstLevel; summary.maxStage = dd.maxStage;
    summary.exitCount = dd.exitCount; summary.totalMoves = dd.totalMoves;
    for (const Action &a : ar.verify.plan) {
      summary.steps.push_back({actionText(a)});
      LubanMove m; m.text = actionText(a); m.group = a.group;
      m.direction = a.dir.name; m.dx = a.dir.dx; m.dy = a.dir.dy; m.dz = a.dir.dz;
      m.steps = a.steps; m.exit = a.exit; m.finalRemoval = a.finalRemoval;
      m.rotate = a.rotate; m.pivot = {a.pivot.x, a.pivot.y, a.pivot.z}; m.rotationDir = a.rotationDir; m.rotationAxis = a.rotationAxis;
      summary.moves.push_back(m);
    }
  } else {
    summary.error = "可拼装，但暂未找到拆解路径。";
  }

  const auto &usedPieces = ar.verify.ok ? ar.verify.pieces : ar.pieces;
  for (const PiecePlacement &piece : usedPieces) {
    LubanPieceView view;
    view.piece = piece.piece;
    view.code = piece.code;
    view.slot = piece.slot;
    view.rotationIndex = piece.rotIndex;
    for (const Vec3 &cell : piece.cells) {
      view.cells.push_back({cell.x, cell.y, cell.z});
    }
    summary.pieces.push_back(view);
  }

  return summary;
}

bool saveEnumCheckpoint(const LubanEnumCheckpoint &cp, const string &path) {
  ofstream f(path);
  if (!f) return false;
  f << "POOL:";
  for (size_t i = 0; i < cp.pool.size(); ++i) { if (i) f << ","; f << cp.pool[i]; }
  f << "\nLENGTH:" << cp.length;
  f << "\nPROCESSED:" << cp.processedCount;
  f << "\nNOROT_SOLUTIONS:" << cp.noRotSolutions;
  f << "\nROT_SOLUTIONS:" << cp.rotSolutions;
  f << "\nNOROT_COMBOS:" << cp.noRotCombos;
  f << "\nROT_COMBOS:" << cp.rotCombos;
  f << "\nTOTAL_SOLUTIONS:" << cp.totalSolutions;
  f << "\n---NONROT_LINES---\n";
  for (auto &line : cp.nonRotLines) f << line << "\n";
  f << "---ROT_LINES---\n";
  for (auto &line : cp.rotLines) f << line << "\n";
  f << "---ASSEMBLY_LINES---\n";
  for (auto &line : cp.assemblyLines) f << line << "\n";
  f << "---END---\n";
  return f.good();
}

static vector<int> parsePoolFromLine(const string &line) {
  vector<int> pool;
  stringstream ss(line);
  string tok;
  while (getline(ss, tok, ',')) { if (!tok.empty()) pool.push_back(atoi(tok.c_str())); }
  return pool;
}

static vector<string> readSection(ifstream &f) {
  vector<string> lines;
  string line;
  while (getline(f, line)) {
    if (line.substr(0, 4) == "---") break;
    lines.push_back(line);
  }
  return lines;
}

LubanEnumCheckpoint loadEnumCheckpoint(const vector<int> &pool, int length, const string &path) {
  LubanEnumCheckpoint cp;
  ifstream f(path);
  if (!f) return cp;
  string line;
  while (getline(f, line)) {
    if (line.rfind("POOL:", 0) == 0) cp.pool = parsePoolFromLine(line.substr(5));
    else if (line.rfind("LENGTH:", 0) == 0) cp.length = atoi(line.substr(7).c_str());
    else if (line.rfind("PROCESSED:", 0) == 0) cp.processedCount = atoi(line.substr(10).c_str());
    else if (line.rfind("NOROT_SOLUTIONS:", 0) == 0) cp.noRotSolutions = atoi(line.substr(16).c_str());
    else if (line.rfind("ROT_SOLUTIONS:", 0) == 0) cp.rotSolutions = atoi(line.substr(14).c_str());
    else if (line.rfind("NOROT_COMBOS:", 0) == 0) cp.noRotCombos = atoi(line.substr(13).c_str());
    else if (line.rfind("ROT_COMBOS:", 0) == 0) cp.rotCombos = atoi(line.substr(11).c_str());
    else if (line.rfind("TOTAL_SOLUTIONS:", 0) == 0) cp.totalSolutions = atoi(line.substr(16).c_str());
    else if (line == "---NONROT_LINES---") cp.nonRotLines = readSection(f);
    else if (line == "---ROT_LINES---") cp.rotLines = readSection(f);
    else if (line == "---ASSEMBLY_LINES---") cp.assemblyLines = readSection(f);
  }
  if (cp.pool != pool || cp.length != length) { cp.valid = false; return cp; }
  cp.valid = (cp.processedCount > 0);
  return cp;
}

// 枚举柱子池中所有 6 根组合（考虑池数量上限），统计可拼/可拆数量
// 剪枝：实心锁的 6 柱实心体素和必须 = 目标锁形体素数（长度6=104）；
// 对每个组合同时统计"非旋转锁(直接可拆)"与"旋转锁(需旋转解锁)"，两类分开计数。
LubanEnumResult enumerateLubanPool(const vector<int> &pool, int length, int expect, bool fast, const LubanEnumCheckpoint &resume) {
  LubanEnumResult er;
  auto t0 = chrono::steady_clock::now();
  int savedBurrLimit = BURR_STATE_LIMIT;
  BURR_STATE_LIMIT = fast ? 40000 : 60000;
  gEnumComboBudgetMs = fast ? 300 : 15000;
  gDisasmTimeoutMs = fast ? 1000 : 1500;
  if (!(length == 6 || length == 8 || length == 10 || length == 12)) {
    er.elapsedMs = 0;
    return er;
  }
  vector<int> sortedPool = pool;
  sort(sortedPool.begin(), sortedPool.end());
  set<int> distinctTypes(pool.begin(), pool.end());
  int P = (int)sortedPool.size();

  int targetSolid = (int)makeStandardTarget(length).required.size();
  int fullCells = 2 * 2 * length;
  auto solidOf = [&](int code) -> int {
    int holes = 0; for (int b = 0; b < 12; ++b) if (code & (1 << b)) ++holes;
    return fullCells - holes;
  };
  vector<int> solid(P);
  for (int i = 0; i < P; ++i) solid[i] = solidOf(sortedPool[i]);
  vector<int> sufMax(P + 1, 0);
  for (int i = P - 1; i >= 0; --i) sufMax[i] = max(solid[i], sufMax[i + 1]);
  vector<int> sufMin(P + 1, 1 << 29);
  for (int i = P - 1; i >= 0; --i) sufMin[i] = min(solid[i], sufMin[i + 1]);

  // --- 生产者-消费者模式：边生成组合边处理，不预存所有组合 ---
  const int QUEUE_CAP = 4096;
  struct ComboQueue {
    mutex mtx;
    condition_variable notFull, notEmpty;
    queue<array<int, 6>> q;
    bool producerDone = false;
    int64_t totalGenerated = 0;
    int64_t totalDeduped = 0;
    void push(array<int, 6> c) {
      unique_lock<mutex> lk(mtx);
      notFull.wait(lk, [&]{ return (int)q.size() < QUEUE_CAP; });
      q.push(c);
      totalGenerated++;
      notEmpty.notify_one();
    }
    bool pop(array<int, 6> &c) {
      unique_lock<mutex> lk(mtx);
      notEmpty.wait(lk, [&]{ return !q.empty() || producerDone; });
      if (q.empty()) return false;
      c = q.front(); q.pop();
      notFull.notify_one();
      return true;
    }
    void finish() {
      unique_lock<mutex> lk(mtx);
      producerDone = true;
      notEmpty.notify_all();
    }
  };

  ComboQueue comboQ;
  vector<int> combo(6);
  set<string> distinctKeys;
  atomic<int64_t> resumeSkipped(0);

  int resumeFrom = 0;
  if (resume.valid) {
    resumeFrom = resume.processedCount;
    cout << "从断点续搜：已跳过前 " << resumeFrom << " 个组合\n" << flush;
  }

  // 生产者线程：生成所有合法组合
  auto producerFn = [&]() {
    function<void(int, int, int)> collectRec = [&](int start, int depth, int curSolid) {
      int remain = 6 - depth;
      if (start < P) {
        int maxAdd = remain * sufMax[start];
        int minAdd = remain * sufMin[start];
        if (curSolid + maxAdd < targetSolid) return;
        if (curSolid + minAdd > targetSolid) return;
      } else if (remain > 0) return;
      if (depth == 6) {
        if (curSolid != targetSolid) return;
        string key;
        for (int v : combo) key += to_string(v) + ",";
        if (!distinctKeys.insert(key).second) { comboQ.totalDeduped++; return; }
        int64_t sk = resumeSkipped.fetch_add(1, memory_order_relaxed) + 1;
        if (sk <= resumeFrom) return;
        array<int, 6> arr;
        for (int j = 0; j < 6; ++j) arr[j] = combo[j];
        comboQ.push(arr);
        return;
      }
      for (int i = start; i < P; ++i) {
        combo[depth] = sortedPool[i];
        collectRec(i + 1, depth + 1, curSolid + solid[i]);
      }
    };
    collectRec(0, 0, 0);
    comboQ.finish();
    cout << "组合生成完毕: 共生成 " << comboQ.totalGenerated << " 个（去重跳过 " << comboQ.totalDeduped << " 个）\n" << flush;
  };

  int nThreads = max(1, (int)thread::hardware_concurrency());
  cout << "版本:v6-stream|disasmMs=" << gDisasmTimeoutMs << "|phase1Nodes=" << 20000 << "|candCap=" << 64 << "|barrLim=" << BURR_STATE_LIMIT << "\n" << flush;
  cout << "开始枚举组合（实心和=" << targetSolid << "，线程数=" << nThreads << (resumeFrom > 0 ? "，从第" + to_string(resumeFrom) + "个开始" : "") << "）...\n" << flush;

  struct ThreadLocalResult {
    int totalCombos = 0;
    int assemblableCombos = 0;
    int disassemblableCombos = 0;
    int totalSolutions = 0;
    int noRotCombos = 0;
    int rotCombos = 0;
    int noRotSolutions = 0;
    int rotSolutions = 0;
    int totalAssembliesChecked = 0;
    int maxCandidatesPerCombo = 0;
    int comboWithMultiCandidates = 0;
    int maxDisasmPerCombo = 0;
    vector<string> nonRotLines;
    vector<string> rotLines;
    vector<string> assemblyLines;
  };

  vector<ThreadLocalResult> localResults(nThreads);
  atomic<int> sharedProcessed(resumeFrom);

  if (resume.valid) {
    ThreadLocalResult &lr0 = localResults[0];
    lr0.totalCombos = resume.processedCount;
    lr0.noRotSolutions = resume.noRotSolutions;
    lr0.rotSolutions = resume.rotSolutions;
    lr0.noRotCombos = resume.noRotCombos;
    lr0.rotCombos = resume.rotCombos;
    lr0.totalSolutions = resume.noRotSolutions + resume.rotSolutions;
    lr0.assemblableCombos = resume.noRotCombos + resume.rotCombos;
    lr0.disassemblableCombos = lr0.assemblableCombos;
    lr0.nonRotLines = resume.nonRotLines;
    lr0.rotLines = resume.rotLines;
    lr0.assemblyLines = resume.assemblyLines;
  }

  mutex outputMutex;
  auto writeOutputFiles = [&]() {
    lock_guard<mutex> lock(outputMutex);
    ofstream foutNonRot("enum_nonrot_combos.txt");
    ofstream foutRot("enum_rot_combos.txt");
    ofstream foutAsm("enum_assemblies.txt");
    foutAsm << "# 鲁班锁六柱可拆装配数据\n";
    foutAsm << "# 格式: 编码方位,编码方位,...  (*为旋转锁)\n";
    foutAsm << "# 方位: 左=槽0 右=槽1 前=槽2 后=槽3 下=槽4 上=槽5\n";
    foutAsm << "# 编码: 0-4095 (12位二进制,每位代表一个锯切位置,1023=实心柱)\n";
    foutAsm << "# 旋转锁: 实心柱(1023)需绕Y轴旋转90度后才能拆解\n";
    int sumNr = 0, sumRot = 0, sumSol = 0;
    for (auto &lr : localResults) {
      for (auto &line : lr.nonRotLines) foutNonRot << line << "\n";
      for (auto &line : lr.rotLines) foutRot << line << "\n";
      for (auto &line : lr.assemblyLines) foutAsm << line << "\n";
      sumNr += lr.noRotSolutions;
      sumRot += lr.rotSolutions;
      sumSol += lr.totalSolutions;
    }
    foutAsm << "# ---- 统计 ----\n";
    foutAsm << "# 非旋转锁: " << sumNr << " 个\n";
    foutAsm << "# 旋转锁(*): " << sumRot << " 个\n";
    foutAsm << "# 合计: " << sumSol << " 个\n";
    if (expect >= 0) {
      foutAsm << "# BurrTools期望: " << expect << " ";
      if (sumNr == expect) foutAsm << "[一致]\n"; else foutAsm << "[不一致](差" << (sumNr - expect) << ")\n";
    }
    foutAsm.flush();
    ofstream foutProg("enum_progress.txt");
    foutProg << "DONE|nonrot=" << sumNr << "|rotsol=" << sumRot << "|total=" << sumSol << endl;
  };

  auto worker = [&](int tid) {
    ThreadLocalResult &lr = localResults[tid];
    array<int, 6> c;
    while (comboQ.pop(c)) {
      if (gEnumStopRequested) break;
      lr.totalCombos++;

      bool hasRotPiece = false;
      int rotTargetCells = 2 * 2 * length - 10;
      for (int v : c) if ((int)pillarCodeToLocalVoxels(v, length).size() == rotTargetCells) { hasRotPiece = true; break; }

      vector<int> cv(c.begin(), c.end());
      AssemblyResult ar = findAssemblyFromCodes(cv, length, /*enumerateAll=*/!fast, /*noRotation=*/!hasRotPiece,
                                                /*rotOnce=*/fast, /*candidateCap=*/fast ? 256 : 64);

      string key;
      for (int v : c) key += to_string(v) + ",";

      if (fast) {
        if (ar.ok) {
          lr.assemblableCombos++;
          lr.disassemblableCombos++;
          if (ar.verify.rotatedShape) { lr.rotCombos++; lr.rotSolutions++; lr.rotLines.push_back(key); }
          else { lr.noRotCombos++; lr.noRotSolutions++; lr.nonRotLines.push_back(key); }
          lr.totalSolutions++;
        }
      } else if (ar.disasmSolutionCountNoRot > 0) {
        lr.assemblableCombos++;
        lr.disassemblableCombos++;
        lr.noRotCombos++;
        lr.totalSolutions += ar.disasmSolutionCountNoRot;
        lr.noRotSolutions += ar.disasmSolutionCountNoRot;
        lr.nonRotLines.push_back(key);
        for (const auto& a : ar.disasmAsmsNoRot) lr.assemblyLines.push_back(a);
        lr.totalAssembliesChecked += ar.checkedAssemblies;
        if (ar.checkedAssemblies > lr.maxCandidatesPerCombo) lr.maxCandidatesPerCombo = ar.checkedAssemblies;
        if (ar.checkedAssemblies > 1) lr.comboWithMultiCandidates++;
        int localDisasm = ar.disasmSolutionCountNoRot + ar.disasmSolutionCountRot;
        if (localDisasm > lr.maxDisasmPerCombo) lr.maxDisasmPerCombo = localDisasm;
      } else if (ar.disasmSolutionCountRot > 0) {
        lr.assemblableCombos++;
        lr.disassemblableCombos++;
        lr.rotCombos++;
        lr.totalSolutions += ar.disasmSolutionCountRot;
        lr.rotSolutions += ar.disasmSolutionCountRot;
        lr.rotLines.push_back(key);
        for (const auto& a : ar.disasmAsmsRot) lr.assemblyLines.push_back(a + "*");
        lr.totalAssembliesChecked += ar.checkedAssemblies;
        if (ar.checkedAssemblies > lr.maxCandidatesPerCombo) lr.maxCandidatesPerCombo = ar.checkedAssemblies;
        if (ar.checkedAssemblies > 1) lr.comboWithMultiCandidates++;
        int localDisasm = ar.disasmSolutionCountNoRot + ar.disasmSolutionCountRot;
        if (localDisasm > lr.maxDisasmPerCombo) lr.maxDisasmPerCombo = localDisasm;
      }

      int pc = sharedProcessed.fetch_add(1, memory_order_relaxed) + 1;
      if (pc % 200 == 0) {
        int totalNr = 0, totalRot = 0, totalSol = 0;
        for (auto &lr2 : localResults) { totalNr += lr2.noRotCombos; totalRot += lr2.rotCombos; totalSol += lr2.totalSolutions; }
        cout << "  ... 已处理 " << pc << " 个组合（非旋转 " << totalNr << " / 旋转 " << totalRot << "）\n" << flush;
        if (gEnumProgressCallback) gEnumProgressCallback(pc, totalNr, totalRot, totalSol);
        writeOutputFiles();
      }
    }
  };

  vector<thread> threads;
  threads.emplace_back(producerFn);
  for (int t = 0; t < nThreads; ++t)
    threads.emplace_back(worker, t);
  for (auto &t : threads) t.join();

  if (gEnumStopRequested) {
    LubanEnumCheckpoint cp;
    cp.pool = sortedPool;
    cp.length = length;
    cp.processedCount = sharedProcessed.load();
    for (auto &lr : localResults) {
      cp.noRotSolutions += lr.noRotSolutions;
      cp.rotSolutions += lr.rotSolutions;
      cp.noRotCombos += lr.noRotCombos;
      cp.rotCombos += lr.rotCombos;
      cp.totalSolutions += lr.totalSolutions;
      for (auto &line : lr.nonRotLines) cp.nonRotLines.push_back(line);
      for (auto &line : lr.rotLines) cp.rotLines.push_back(line);
      for (auto &line : lr.assemblyLines) cp.assemblyLines.push_back(line);
    }
    saveEnumCheckpoint(cp);
    cout << "已保存断点（已处理 " << cp.processedCount << " 个组合），下次可续搜\n" << flush;
  }

  writeOutputFiles();

  for (auto &lr : localResults) {
    er.totalCombos += lr.totalCombos;
    er.assemblableCombos += lr.assemblableCombos;
    er.disassemblableCombos += lr.disassemblableCombos;
    er.totalSolutions += lr.totalSolutions;
    er.noRotCombos += lr.noRotCombos;
    er.rotCombos += lr.rotCombos;
    er.noRotSolutions += lr.noRotSolutions;
    er.rotSolutions += lr.rotSolutions;
    er.totalAssembliesChecked += lr.totalAssembliesChecked;
    if (lr.maxCandidatesPerCombo > er.maxCandidatesPerCombo) er.maxCandidatesPerCombo = lr.maxCandidatesPerCombo;
    er.comboWithMultiCandidates += lr.comboWithMultiCandidates;
    if (lr.maxDisasmPerCombo > er.maxDisasmPerCombo) er.maxDisasmPerCombo = lr.maxDisasmPerCombo;
  }

  er.distinctCombos = (int)distinctKeys.size();
  er.elapsedMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();

  cout << "=== 枚举模式统计 ===\n";
  cout << "柱子池大小: " << P << "（不同柱型 " << distinctTypes.size() << " 种）\n";
  cout << "线程数: " << nThreads << "\n";
  cout << "目标实心体素数: " << targetSolid << "（仅枚举实心和=此值的组合）\n";
  cout << "满足体素和的组合数: " << er.totalCombos << "\n";
  cout << "不同柱组合(类型multiset): " << er.distinctCombos << "\n";
  cout << "可拼组合: " << er.assemblableCombos << "\n";
  cout << "可拼且可拆组合: " << er.disassemblableCombos << "\n";
  cout << "---- 按是否需要旋转区分 ----\n";
  cout << "非旋转锁组合(直接可拆): " << er.noRotCombos << "\n";
  cout << "  非旋转可拆装配方案数(含朝向): " << er.noRotSolutions << "\n";
  cout << "旋转锁组合(需旋转解锁): " << er.rotCombos << "\n";
  cout << "  旋转可拆装配方案数(含朝向): " << er.rotSolutions << "\n";
  cout << "可拆装配方案总数(非旋转+旋转,含朝向): " << er.totalSolutions << "\n";
  if (expect >= 0) {
    cout << "BurrTools 解数(参考): " << expect << "\n";
    if (fast) {
      cout << "注意: --fast 仅统计可拆组合数(每组合计1)，未枚举所有朝向方案，暂不与 BurrTools 对比。\n";
    } else {
      cout << "对比: 可拆装配方案 " << (er.totalSolutions == expect ? "[一致]" : "[不一致]")
           << " (差 " << (er.totalSolutions - expect) << ")\n";
    }
  }
  cout << "用时: " << er.elapsedMs << " ms\n";
  cout << "---- 诊断：汇编器候选数统计 ----\n";
  cout << "总装配候选数(所有组合合计): " << er.totalAssembliesChecked << "\n";
  cout << "单组合最多装配候选: " << er.maxCandidatesPerCombo << "\n";
  cout << "有>1个装配候选的组合数: " << er.comboWithMultiCandidates << "\n";
  cout << "单组合最多可拆装配数: " << er.maxDisasmPerCombo << "\n";
  if (!gEnumStopRequested) remove("enum_checkpoint.txt");
  BURR_STATE_LIMIT = savedBurrLimit;
  gEnumComboBudgetMs = -1;
  gDisasmTimeoutMs = 2000;
  return er;
}
