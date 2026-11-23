#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <GL/gl.h>

#include "luban_core_api.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "opengl32.lib")
#endif

namespace {

constexpr int IDC_SOLVE = 1001;
constexpr int IDC_RESET = 1002;
constexpr int IDC_PREV = 1003;
constexpr int IDC_NEXT = 1004;
constexpr int IDC_AUTO_DIS = 1005;
constexpr int IDC_AUTO_ASM = 1006;
constexpr int IDC_LENGTH = 1007;
constexpr int IDC_INPUT_RESET = 1008;
constexpr int IDC_PIECE_SELECT = 1009;
constexpr int IDC_TIMER = 2001;
constexpr int IDC_SOLVE_TIMER = 2002;
constexpr int IDC_ANIM_TIMER = 2003;
constexpr int IDC_CODE_BASE = 2100;
constexpr int IDC_SHOW_BASE = 3200;
constexpr int IDC_PIECE_BASE = 3300;
constexpr int IDC_VIEWMODE_BASE = 3400;
constexpr int IDC_ACT_BASE = 3500;
constexpr int IDC_RSTYLE_BASE = 3600;
constexpr UINT WM_SOLVE_DONE = WM_APP + 1;
constexpr UINT ID_FILE_OPEN = 9001;
constexpr UINT ID_FILE_SAVE = 9002;
constexpr UINT ID_FILE_SAVEAS = 9003;

const std::array<std::array<int, 3>, 12> EDIT_IP = {{
  {{1,1,1}}, {{1,1,2}}, {{1,1,3}}, {{1,1,4}},
  {{0,1,1}}, {{0,1,2}}, {{0,1,3}}, {{0,1,4}},
  {{1,0,2}}, {{1,0,3}}, {{0,0,2}}, {{0,0,3}}
}};

/* 活动块显示顺序：与图2 斜视角的视觉上下一致（z 低→高），紫块仍放最下；索引为 EDIT_IP 下标 */
static const int ACT_ORDER[12] = {4, 0, 5, 1, 6, 2, 7, 3, 11, 9, 10, 8};
static const wchar_t *ACT_DESC[12] = {
  L"蓝 · 上方位 · 2", L"绿 · 右前杆 2", L"绿 · 右后杆 8", L"蓝 · 下方位 · 2",
  L"蓝 · 上方位 · 1", L"绿 · 左前杆 3", L"绿 · 左后杆 4", L"蓝 · 下方位 · 1",
  L"紫 · 左柱后",     L"紫 · 右柱后",   L"紫 · 左柱前",   L"紫 · 右柱前"
};
static COLORREF actColor(int i) {
  int y = EDIT_IP[i][1], z = EDIT_IP[i][2];
  if (y == 0) return RGB(143, 79, 187);           // 紫：左右两柱
  if (z == 1 || z == 4) return RGB(47, 131, 207); // 蓝：上/下方位
  return RGB(44, 154, 92);                        // 绿：前/后杆
}

const std::string HOLLOW_TARGET_SHAPE =
  "_____________####__####_______________##____##___#++#__#++#___##____##____##__##++###++++##++++###++##__##____##__##++###++++##++++###++##__##____##____##___#++#__#++#___##____##_______________####__####_____________";

HINSTANCE g_inst = nullptr;
HWND g_main = nullptr;
HWND g_codeLabels[6] = {};
HWND g_edits[6] = {};
HWND g_lengthLabel = nullptr;
HWND g_length = nullptr;
HWND g_editorTitle = nullptr;
HWND g_actTitle = nullptr;
HWND g_codeTitle = nullptr;
HWND g_summary = nullptr;
HWND g_steps = nullptr;
HWND g_view = nullptr;
HWND g_status = nullptr;
HWND g_buttons[5] = {};
HWND g_actButtons[12] = {};
WNDPROC g_actOldProc[12] = {};
int g_hoverIdx = -1;
HWND g_rstyleLabel = nullptr;
HWND g_rstyleButtons[3] = {};
enum RenderStyle { RS_VOXEL = 0, RS_EDGES = 1, RS_STL = 2 };
int g_renderStyle = RS_VOXEL;
HWND g_showChecks[6] = {};
HWND g_pieceButtons[6] = {};
HWND g_viewModeButtons[6] = {};
HDC g_glDc = nullptr;
HGLRC g_glRc = nullptr;
HBRUSH g_windowBrush = nullptr;
HBRUSH g_panelBrush = nullptr;
HBRUSH g_editBrush = nullptr;
HBRUSH g_viewFrameBrush = nullptr;
HFONT g_uiFont = nullptr;
HFONT g_bigFont = nullptr;

std::vector<LubanPieceView> g_initialPieces;
std::vector<LubanPieceView> g_pieces;
std::vector<LubanMove> g_moves;
int g_lengthValue = 6;
int g_currentStep = 0;
int g_autoDirection = 1;
int g_activePiece = 0;
double g_yaw = 0.60;
double g_pitch = -28.0;
double g_zoom = 1.0;
bool g_dragging = false;
bool g_pieceVisible[6] = {true, true, true, true, true, true};
int g_pieceDisplayMode[6] = {0, 0, 0, 0, 0, 0};
bool g_solving = false;
bool g_animating = false;
int g_animSign = 1;
int g_animMoveIndex = -1;
DWORD g_animStartTick = 0;
DWORD g_animDurationMs = 0;
DWORD g_solveStartTick = 0;
POINT g_lastMouse = {};

struct SolveRequest {
  HWND hwnd = nullptr;
  std::vector<int> codes;
  int length = 6;
};

struct PlaybackStage {
  int start = 0;
  int end = 0;
  int moveCount = 0;
  std::wstring text;
};

struct RenderCell {
  int piece = 0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  int gx = 0, gy = 0, gz = 0;  // 网格整数坐标（未居中、未动画），用于遮挡判定
  bool wire = false;   // 线框件：不作为遮挡物
  double f = 1.0;      // 明度系数（高亮/已移除）
};

std::vector<PlaybackStage> g_stages;

void updateButtons();
void updatePieceButtons();
void updatePieceDisplayButtons();
void stopPlaybackAnimation();


void invalidateView() {
  if (g_view) InvalidateRect(g_view, nullptr, FALSE);
}

std::wstring toWide(const std::string &s) {
  if (s.empty()) return L"";
  int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring out(std::max(0, count - 1), L'\0');
  if (count > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), count);
  return out;
}

std::string toUtf8(const std::wstring &s) {
  if (s.empty()) return "";
  int count = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(std::max(0, count - 1), '\0');
  if (count > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), count, nullptr, nullptr);
  return out;
}

std::wstring fmt(const wchar_t *format, ...) {
  wchar_t buffer[1024];
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(buffer, _TRUNCATE, format, args);
  va_end(args);
  return buffer;
}

std::string key3s(int x, int y, int z) {
  return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
}

int extLenForTarget(int length) {
  return std::max(0, (length - 6) / 2);
}

LubanCell assemblySlotMapForTarget(int slot, int x, int y, int z, int length) {
  int ext = extLenForTarget(length);
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

std::set<std::string> standardTargetSet(int length) {
  std::set<std::string> out;
  for (int slot = 0; slot < 6; ++slot) {
    for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < length; ++z) {
      LubanCell c = assemblySlotMapForTarget(slot, x, y, z, length);
      out.insert(key3s(c.x, c.y, c.z));
    }
  }
  return out;
}

std::set<std::string> shiftedBaseTargetSet(int length) {
  int ext = extLenForTarget(length);
  std::set<std::string> out;
  for (const std::string &k : standardTargetSet(6)) {
    int x = 0, y = 0, z = 0;
    char c1 = 0, c2 = 0;
    std::stringstream ss(k);
    ss >> x >> c1 >> y >> c2 >> z;
    out.insert(key3s(x + ext, y + ext, z + ext));
  }
  return out;
}

void targetSetsForPieces(const std::vector<LubanPieceView> &pieces, int length,
                         std::set<std::string> &required, std::set<std::string> &allowed) {
  int solidCount = 0;
  for (const auto &piece : pieces) solidCount += (int)piece.cells.size();
  std::set<std::string> standard = standardTargetSet(length);
  if (solidCount == (int)standard.size()) {
    required = standard;
    allowed = standard;
    return;
  }

  int ext = extLenForTarget(length);
  for (int i = 0; i < (int)HOLLOW_TARGET_SHAPE.size(); ++i) {
    char ch = HOLLOW_TARGET_SHAPE[i];
    if (ch != '#' && ch != '+') continue;
    int x = i % 6;
    int y = (i / 6) % 6;
    int z = i / 36;
    std::string k = key3s(x + ext, y + ext, z + ext);
    allowed.insert(k);
    if (ch == '#') required.insert(k);
  }
  if (length > 6) {
    std::set<std::string> fullLong = standardTargetSet(length);
    std::set<std::string> shiftedCore = shiftedBaseTargetSet(length);
    for (const std::string &k : fullLong) {
      if (shiftedCore.count(k)) continue;
      allowed.insert(k);
      required.insert(k);
    }
  }
}

COLORREF pieceColor(int piece) {
  static const COLORREF colors[] = {
    RGB(52, 152, 219),
    RGB(46, 204, 113),
    RGB(245, 166, 35),
    RGB(231, 76, 60),
    RGB(155, 89, 182),
    RGB(26, 188, 156)
  };
  if (piece <= 0) return RGB(180, 180, 180);
  return colors[(piece - 1) % 6];
}

COLORREF shade(COLORREF c, int delta) {
  auto clamp = [](int v) { return std::max(0, std::min(255, v)); };
  return RGB(clamp(GetRValue(c) + delta), clamp(GetGValue(c) + delta), clamp(GetBValue(c) + delta));
}

void setFont(HWND hwnd, HFONT font) {
  SendMessage(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

/* 渲染风格记忆：注册表 HKCU\Software\LubanGui\RenderStyle（对应网页版 localStorage jgfRenderStyle） */
static void loadRenderStyle() {
  g_renderStyle = RS_VOXEL;
  HKEY hk = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LubanGui", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
    wchar_t buf[32] = {};
    DWORD sz = sizeof(buf), type = 0;
    if (RegQueryValueExW(hk, L"RenderStyle", nullptr, &type, (BYTE *)buf, &sz) == ERROR_SUCCESS) {
      if (wcscmp(buf, L"edges") == 0) g_renderStyle = RS_EDGES;
      else if (wcscmp(buf, L"stl") == 0) g_renderStyle = RS_STL;
    }
    RegCloseKey(hk);
  }
}
static void saveRenderStyle() {
  HKEY hk = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LubanGui", 0, nullptr, REG_OPTION_NON_VOLATILE,
                      KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
    const wchar_t *v = g_renderStyle == RS_EDGES ? L"edges" : (g_renderStyle == RS_STL ? L"stl" : L"voxel");
    RegSetValueExW(hk, L"RenderStyle", 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
  }
}
static void syncRenderStyleButtons() {
  for (int i = 0; i < 3; ++i) {
    if (!g_rstyleButtons[i]) continue;
    SendMessage(g_rstyleButtons[i], BM_SETCHECK, g_renderStyle == i ? BST_CHECKED : BST_UNCHECKED, 0);
  }
}
static void setRenderStyle(int s) {
  if (s < RS_VOXEL || s > RS_STL) return;
  g_renderStyle = s;
  saveRenderStyle();
  syncRenderStyleButtons();
  invalidateView();
}

HWND makeControl(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, HWND parent, int id) {
  HWND hwnd = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
  setFont(hwnd, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
  return hwnd;
}

std::wstring welcomeText() {
  return L"欢迎使用鲁班锁六柱轻量版求解器\r\n"
         L"\r\n"
         L"左侧：选择柱子后，可点击「活动块」按钮切换 12 个中间格的实/空（彩色=实心、红叉=挖空）；"
         L"鼠标悬停按钮会在 3D 视图中斜线高亮对应格子。\r\n"
         L"中间：OpenGL 预览区，未求解时显示当前编辑柱子，求解后显示锁体。\r\n"
         L"右侧：显示目标锁形、用时、难度、旋转锁信息和拆解步骤。\r\n"
         L"\r\n"
         L"鼠标拖动可旋转视角，滚轮可缩放。";
}

void drawPanel(HDC hdc, const RECT &rc, const wchar_t *title) {
  FillRect(hdc, &rc, g_panelBrush);
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(188, 196, 207));
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);

  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, RGB(35, 43, 54));
  RECT textRc{rc.left + 10, rc.top + 6, rc.right - 10, rc.top + 26};
  DrawTextW(hdc, title, -1, &textRc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

void paintMainBackground(HWND hwnd, HDC hdc) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(hdc, &rc, g_windowBrush);

  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  int margin = 10;
  int leftW = 280;
  int rightW = 360;
  int topH = h - 42;
  RECT left{margin, margin, leftW + margin, topH};
  RECT center{leftW + margin * 2 - 8, margin, w - rightW - margin * 2 + 8, topH};
  RECT right{w - rightW - margin, margin, w - margin, topH};
  drawPanel(hdc, left, L"柱子编辑 / 编码输入");
  drawPanel(hdc, center, L"锁体预览");
  drawPanel(hdc, right, L"求解结果 / 拆解步骤");
}

int selectedLength() {
  int idx = (int)SendMessage(g_length, CB_GETCURSEL, 0, 0);
  static const int values[] = {6, 8, 10, 12};
  if (idx < 0 || idx >= 4) return 6;
  return values[idx];
}

int extLenForPreview(int length) {
  return std::max(0, (length - 6) / 2);
}

bool readCodes(std::vector<int> &codes) {
  codes.clear();
  wchar_t text[64];
  for (int i = 0; i < 6; ++i) {
    GetWindowTextW(g_edits[i], text, 64);
    wchar_t *end = nullptr;
    long value = wcstol(text, &end, 10);
    if (end == text || value < 0 || value > 4095) return false;
    codes.push_back((int)value);
  }
  return true;
}

int editCode(int pieceIndex) {
  wchar_t text[64] = {};
  GetWindowTextW(g_edits[pieceIndex], text, 64);
  wchar_t *end = nullptr;
  long value = wcstol(text, &end, 10);
  if (end == text) return 0;
  return std::max(0L, std::min(4095L, value));
}

void setEditCode(int pieceIndex, int code) {
  code = std::max(0, std::min(4095, code));
  SetWindowTextW(g_edits[pieceIndex], std::to_wstring(code).c_str());
}

void updateCellButtonsFromCode() {
  // 活动块按钮为自绘，无效化即可按当前编码重画
  for (int i = 0; i < 12; ++i)
    if (g_actButtons[i]) InvalidateRect(g_actButtons[i], nullptr, FALSE);
  invalidateView();
}

void toggleEditCell(int editIdx) {
  if (editIdx < 0 || editIdx >= 12) return;
  int code = editCode(g_activePiece);
  code ^= (1 << editIdx);
  setEditCode(g_activePiece, code);
  updateCellButtonsFromCode();
}

/* 活动块按钮 subclass：悬停 -> 3D 暴露面斜线高亮 */
static LRESULT CALLBACK ActBtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  int id = (int)(INT_PTR)GetWindowLongPtr(h, GWLP_ID);
  if (id >= IDC_ACT_BASE && id < IDC_ACT_BASE + 12) {
    int idx = ACT_ORDER[id - IDC_ACT_BASE];
    if (msg == WM_MOUSEMOVE) {
      if (g_hoverIdx != idx) { g_hoverIdx = idx; invalidateView(); }
      TRACKMOUSEEVENT tme; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE;
      tme.hwndTrack = h; tme.dwHoverTime = 0; TrackMouseEvent(&tme);
    } else if (msg == WM_MOUSELEAVE) {
      if (g_hoverIdx == idx) { g_hoverIdx = -1; invalidateView(); }
    }
  }
  WNDPROC old = (id >= IDC_ACT_BASE && id < IDC_ACT_BASE + 12)
                  ? g_actOldProc[id - IDC_ACT_BASE] : nullptr;
  return CallWindowProc(old ? old : DefWindowProc, h, msg, wp, lp);
}

/* 活动块自绘：左侧色块（实心=彩色、挖空=白底红叉）+ 右侧描述 */
static void drawActButton(DRAWITEMSTRUCT *di) {
  int id = (int)di->CtlID;
  if (id < IDC_ACT_BASE || id >= IDC_ACT_BASE + 12) return;
  int i = ACT_ORDER[id - IDC_ACT_BASE];
  HDC hdc = di->hDC;
  RECT rc = di->rcItem;
  int code = editCode(g_activePiece);
  bool solid = (code & (1 << i)) == 0;

  HBRUSH bg = CreateSolidBrush((di->itemState & ODS_SELECTED) ? RGB(224, 232, 228) : RGB(250, 250, 248));
  FillRect(hdc, &rc, bg);
  DeleteObject(bg);
  DWORD edge = (di->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED;
  DrawEdge(hdc, &rc, edge, BF_RECT | BF_MIDDLE);

  int bx = rc.left + 4;
  int by = rc.top + (rc.bottom - rc.top - 13) / 2;
  RECT br{ bx, by, bx + 13, by + 13 };
  HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
  HBRUSH ob = (HBRUSH)SelectObject(hdc, nb);
  if (solid) {
    HBRUSH sb = CreateSolidBrush(actColor(i));
    FillRect(hdc, &br, sb);
    DeleteObject(sb);
    HPEN op = (HPEN)SelectObject(hdc, CreatePen(PS_SOLID, 1, RGB(20, 25, 20)));
    Rectangle(hdc, bx, by, bx + 13, by + 13);
    DeleteObject(SelectObject(hdc, op));
  } else {
    HBRUSH wb = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(hdc, &br, wb);
    HPEN op = (HPEN)SelectObject(hdc, CreatePen(PS_SOLID, 1, RGB(184, 74, 52)));
    Rectangle(hdc, bx, by, bx + 13, by + 13);
    MoveToEx(hdc, bx + 2, by + 2, nullptr); LineTo(hdc, bx + 11, by + 11);
    MoveToEx(hdc, bx + 11, by + 2, nullptr); LineTo(hdc, bx + 2, by + 11);
    DeleteObject(SelectObject(hdc, op));
  }
  SelectObject(hdc, ob);

  SetBkMode(hdc, TRANSPARENT);
  HFONT of = (HFONT)SelectObject(hdc, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
  RECT tr{ bx + 17, rc.top, rc.right - 3, rc.bottom };
  DrawTextW(hdc, ACT_DESC[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(hdc, of);
}

void selectEditPiece(int index) {
  g_activePiece = std::max(0, std::min(5, index));
  updatePieceButtons();
  updateCellButtonsFromCode();
}

void updatePieceButtons() {
  for (int i = 0; i < 6; ++i) {
    std::wstring text = i == g_activePiece ? fmt(L"[柱子 %d]", i + 1) : fmt(L"柱子 %d", i + 1);
    SetWindowTextW(g_pieceButtons[i], text.c_str());
  }
}

void updatePieceDisplayButtons() {
  for (int i = 0; i < 6; ++i) {
    const wchar_t *prefix = L"柱子";
    if (g_pieceDisplayMode[i] == 1) prefix = L"线框";
    if (g_pieceDisplayMode[i] == 2) prefix = L"隐藏";
    SetWindowTextW(g_viewModeButtons[i], fmt(L"%s%d", prefix, i + 1).c_str());
    SendMessage(g_showChecks[i], BM_SETCHECK, g_pieceDisplayMode[i] == 2 ? BST_UNCHECKED : BST_CHECKED, 0);
  }
}

void setPieceDisplayMode(int index, int mode) {
  if (index < 0 || index >= 6) return;
  g_pieceDisplayMode[index] = ((mode % 3) + 3) % 3;
  g_pieceVisible[index] = g_pieceDisplayMode[index] != 2;
  updatePieceDisplayButtons();
  invalidateView();
}

void resetAllInputs() {
  stopPlaybackAnimation();
  if (g_main) {
    KillTimer(g_main, IDC_TIMER);
    KillTimer(g_main, IDC_SOLVE_TIMER);
  }
  g_solving = false;
  g_solveStartTick = 0;
  for (int i = 0; i < 6; ++i) {
    setEditCode(i, 0);
    g_pieceVisible[i] = true;
    g_pieceDisplayMode[i] = 0;
    SendMessage(g_showChecks[i], BM_SETCHECK, BST_CHECKED, 0);
  }
  updatePieceDisplayButtons();
  selectEditPiece(0);
  g_initialPieces.clear();
  g_pieces.clear();
  g_moves.clear();
  g_stages.clear();
  g_currentStep = 0;
  SetWindowTextW(g_summary, L"已重置输入。可以重新编辑柱子编码后求解。");
  SetWindowTextW(g_steps, L"");
  updateButtons();
  invalidateView();
}

std::vector<LubanCell> previewCellsFromCode(int code, int length) {
  int ext = extLenForPreview(length);
  std::set<std::wstring> holes;
  for (int i = 0; i < 12; ++i) {
    if (code & (1 << i)) {
      holes.insert(fmt(L"%d,%d,%d", EDIT_IP[i][0], EDIT_IP[i][1], EDIT_IP[i][2] + ext));
    }
  }

  std::vector<LubanCell> cells;
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < length; ++z) {
        if (!holes.count(fmt(L"%d,%d,%d", x, y, z))) cells.push_back({x, y, z});
      }
    }
  }
  return cells;
}

LubanCell rotateY(const LubanCell &cell, const LubanCell &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int nx = dir > 0 ? z : -z;
  int nz = dir > 0 ? -x : x;
  return {nx + pivot.x, y + pivot.y, nz + pivot.z};
}

LubanCell rotateX(const LubanCell &cell, const LubanCell &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int ny = dir > 0 ? -z : z;
  int nz = dir > 0 ? y : -y;
  return {x + pivot.x, ny + pivot.y, nz + pivot.z};
}

LubanCell rotateZ(const LubanCell &cell, const LubanCell &pivot, int dir) {
  int x = cell.x - pivot.x;
  int y = cell.y - pivot.y;
  int z = cell.z - pivot.z;
  int nx = dir > 0 ? -y : y;
  int ny = dir > 0 ? x : -x;
  return {nx + pivot.x, ny + pivot.y, z + pivot.z};
}

void applyMoveToPieces(std::vector<LubanPieceView> &pieces, const LubanMove &move, int sign) {
  std::set<int> group(move.group.begin(), move.group.end());
  for (auto &piece : pieces) {
    if (!group.count(piece.piece)) continue;
    for (auto &cell : piece.cells) {
      if (move.rotate) {
        switch (move.rotationAxis) {
          case 'x': cell = rotateX(cell, move.pivot, move.rotationDir * sign); break;
          case 'z': cell = rotateZ(cell, move.pivot, move.rotationDir * sign); break;
          default:  cell = rotateY(cell, move.pivot, move.rotationDir * sign); break;
        }
      } else {
        cell.x += move.dx * move.steps * sign;
        cell.y += move.dy * move.steps * sign;
        cell.z += move.dz * move.steps * sign;
      }
    }
  }
}

void applyMove(const LubanMove &move) {
  applyMoveToPieces(g_pieces, move, 1);
}

void stopPlaybackAnimation() {
  if (!g_animating) return;
  g_animating = false;
  g_animMoveIndex = -1;
  g_animStartTick = 0;
  g_animDurationMs = 0;
  if (g_main) KillTimer(g_main, IDC_ANIM_TIMER);
}

std::vector<LubanPieceView> lockedDisplayPieces(const LubanSolveSummary &result) {
  // 求解器返回的pieces是旋转前锁形，第一步是旋转解锁，与网页版行为一致。
  return result.pieces;
}

std::wstring stageText(const LubanMove &move, int moveCount) {
  std::wstring text = toWide(move.text);
  if (!move.rotate && moveCount > 1) {
    text += fmt(L"（包含内部调整 %d 次）", moveCount - 1);
  }
  return text;
}

std::vector<PlaybackStage> makePlaybackStages(const std::vector<LubanMove> &moves) {
  std::vector<PlaybackStage> stages;
  int start = 0;
  for (int i = 0; i < (int)moves.size(); ++i) {
    if (moves[i].rotate) {
      stages.push_back({i, i, 1, stageText(moves[i], 1)});
      start = i + 1;
      continue;
    }
    if (!moves[i].exit) continue;
    int count = i - start + 1;
    stages.push_back({start, i, count, stageText(moves[i], count)});
    start = i + 1;
  }
  if (stages.empty() && !moves.empty()) {
    stages.push_back({0, (int)moves.size() - 1, (int)moves.size(), stageText(moves.back(), (int)moves.size())});
  }
  return stages;
}

int stageIndexForStep(int step) {
  if (g_stages.empty()) return -1;
  if (step >= (int)g_moves.size()) return (int)g_stages.size() - 1;
  for (int i = 0; i < (int)g_stages.size(); ++i) {
    const auto &stage = g_stages[i];
    if (step >= stage.start && step <= stage.end) return i;
    if (step < stage.start) return i;
  }
  return (int)g_stages.size() - 1;
}

void rebuildToStep(int step) {
  stopPlaybackAnimation();
  step = std::max(0, std::min(step, (int)g_moves.size()));
  g_pieces = g_initialPieces;
  g_currentStep = 0;
  for (int i = 0; i < step; ++i) {
    applyMove(g_moves[i]);
    ++g_currentStep;
  }
  invalidateView();
}

void updateButtons() {
  bool hasMoves = !g_moves.empty();
  EnableWindow(g_buttons[0], !g_solving && !g_animating && hasMoves && g_currentStep > 0);
  EnableWindow(g_buttons[1], !g_solving && !g_animating && hasMoves && g_currentStep > 0);
  EnableWindow(g_buttons[2], !g_solving && !g_animating && hasMoves && g_currentStep < (int)g_moves.size());
  EnableWindow(g_buttons[3], !g_solving && !g_animating && hasMoves && g_currentStep < (int)g_moves.size());
  EnableWindow(g_buttons[4], !g_solving && !g_animating && hasMoves && g_currentStep > 0);
  if (g_main) {
    EnableWindow(GetDlgItem(g_main, IDC_SOLVE), !g_solving && !g_animating);
    EnableWindow(GetDlgItem(g_main, IDC_INPUT_RESET), !g_solving && !g_animating);
  }
  if (!g_solving) {
    int stageIndex = stageIndexForStep(g_currentStep);
    if (stageIndex >= 0) {
      const auto &stage = g_stages[stageIndex];
      int doneInStage = std::max(0, std::min(stage.moveCount, g_currentStep - stage.start));
      SetWindowTextW(g_status, fmt(L"总小步 %d / %d；第 %d 阶段小步 %d / %d",
                                   g_currentStep, (int)g_moves.size(), stageIndex + 1, doneInStage, stage.moveCount).c_str());
    } else {
      SetWindowTextW(g_status, fmt(L"步骤 %d / %d", g_currentStep, (int)g_moves.size()).c_str());
    }
  }
  if (g_steps && g_stages.size() > 0) {
    int sel = stageIndexForStep(g_currentStep);
    if (sel >= 0 && sel < (int)g_stages.size()) {
      // 选中该步骤对应的行
      std::wstring allText;
      for (size_t i = 0; i < g_stages.size(); ++i) {
        std::wstring line = fmt(L"%d. %s\r\n", (int)i + 1, g_stages[i].text.c_str());
        allText += line;
      }
      // 找到第 sel 行的字符位置
      int charStart = 0, charEnd = 0;
      for (int i = 0; i <= sel; ++i) {
        std::wstring line = fmt(L"%d. %s\r\n", i + 1, g_stages[i].text.c_str());
        if (i < sel) charStart += (int)line.size();
        charEnd = charStart + (int)line.size() - 2; // 不选 \r\n
      }
      SendMessageW(g_steps, EM_SETSEL, charStart, charEnd);
      SendMessage(g_steps, EM_SCROLLCARET, 0, 0);
    }
  }
}

DWORD animationDurationForMove(const LubanMove &move) {
  if (move.rotate) return 650;
  int grids = std::max(1, std::abs(move.steps));
  return (DWORD)std::max(360, std::min(1400, grids * 120));
}

void startStepAnimation(int sign) {
  if (g_animating || g_moves.empty()) return;
  int index = sign > 0 ? g_currentStep : g_currentStep - 1;
  if (index < 0 || index >= (int)g_moves.size()) return;
  g_animating = true;
  g_animSign = sign > 0 ? 1 : -1;
  g_animMoveIndex = index;
  g_animStartTick = GetTickCount();
  g_animDurationMs = animationDurationForMove(g_moves[index]);
  SetTimer(g_main, IDC_ANIM_TIMER, 16, nullptr);
  updateButtons();
  invalidateView();
}

void finishStepAnimation() {
  if (!g_animating || g_animMoveIndex < 0 || g_animMoveIndex >= (int)g_moves.size()) {
    stopPlaybackAnimation();
    return;
  }
  int index = g_animMoveIndex;
  int sign = g_animSign;
  stopPlaybackAnimation();
  if (sign > 0) {
    applyMove(g_moves[index]);
    g_currentStep = std::min((int)g_moves.size(), g_currentStep + 1);
  } else {
    rebuildToStep(g_currentStep - 1);
  }
  updateButtons();
  invalidateView();
}

double animationProgress() {
  if (!g_animating || !g_animDurationMs) return 0.0;
  double t = (double)(GetTickCount() - g_animStartTick) / (double)g_animDurationMs;
  if (t < 0.0) return 0.0;
  if (t > 1.0) return 1.0;
  return t;
}

void updatePlaybackAnimation() {
  if (!g_animating) return;
  if (animationProgress() >= 1.0) {
    finishStepAnimation();
    return;
  }
  invalidateView();
}

void stepForward() {
  if (g_currentStep >= (int)g_moves.size()) return;
  startStepAnimation(1);
}

void stepBackward() {
  if (g_currentStep <= 0) return;
  startStepAnimation(-1);
}

bool setupOpenGL(HWND hwnd) {
  g_glDc = GetDC(hwnd);
  PIXELFORMATDESCRIPTOR pfd = {};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;

  int format = ChoosePixelFormat(g_glDc, &pfd);
  if (!format || !SetPixelFormat(g_glDc, format, &pfd)) return false;
  g_glRc = wglCreateContext(g_glDc);
  if (!g_glRc || !wglMakeCurrent(g_glDc, g_glRc)) return false;

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearColor(0.055f, 0.078f, 0.13f, 1.0f);
  return true;
}

void cleanupOpenGL() {
  if (g_glRc) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_glRc);
    g_glRc = nullptr;
  }
  if (g_glDc && g_view) {
    ReleaseDC(g_view, g_glDc);
    g_glDc = nullptr;
  }
}

void setProjection(HWND hwnd, double span) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int width = std::max(1, (int)(rc.right - rc.left));
  int height = std::max(1, (int)(rc.bottom - rc.top));
  glViewport(0, 0, width, height);
  double aspect = (double)width / (double)height;
  double range = std::max(5.0, span * 0.72 / std::max(0.25, g_zoom));

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  if (aspect >= 1.0) {
    glOrtho(-range * aspect, range * aspect, -range, range, -80.0, 80.0);
  } else {
    glOrtho(-range, range, -range / aspect, range / aspect, -80.0, 80.0);
  }
  glMatrixMode(GL_MODELVIEW);
}

void glColorShade(COLORREF color, int delta) {
  COLORREF c = shade(color, delta);
  glColor3ub(GetRValue(c), GetGValue(c), GetBValue(c));
}

void drawCubeEdges(double x0, double x1, double y0, double y1, double z0, double z1) {
  glBegin(GL_LINES);
  glVertex3d(x0,y0,z0); glVertex3d(x1,y0,z0);
  glVertex3d(x1,y0,z0); glVertex3d(x1,y1,z0);
  glVertex3d(x1,y1,z0); glVertex3d(x0,y1,z0);
  glVertex3d(x0,y1,z0); glVertex3d(x0,y0,z0);
  glVertex3d(x0,y0,z1); glVertex3d(x1,y0,z1);
  glVertex3d(x1,y0,z1); glVertex3d(x1,y1,z1);
  glVertex3d(x1,y1,z1); glVertex3d(x0,y1,z1);
  glVertex3d(x0,y1,z1); glVertex3d(x0,y0,z1);
  glVertex3d(x0,y0,z0); glVertex3d(x0,y0,z1);
  glVertex3d(x1,y0,z0); glVertex3d(x1,y0,z1);
  glVertex3d(x1,y1,z0); glVertex3d(x1,y1,z1);
  glVertex3d(x0,y1,z0); glVertex3d(x0,y1,z1);
  glEnd();
}

void drawCube(double x, double y, double z, COLORREF color, bool wireframe) {
  const double s = 0.48;
  const double x0 = x - s, x1 = x + s;
  const double y0 = y - s, y1 = y + s;
  const double z0 = z - s, z1 = z + s;

  if (wireframe) {
    glColorShade(color, 18);
    drawCubeEdges(x0, x1, y0, y1, z0, z1);
    return;
  }

  glBegin(GL_QUADS);
  glColorShade(color, 24);
  glVertex3d(x0, y1, z0); glVertex3d(x1, y1, z0); glVertex3d(x1, y1, z1); glVertex3d(x0, y1, z1);
  glColorShade(color, -32);
  glVertex3d(x0, y0, z0); glVertex3d(x0, y0, z1); glVertex3d(x1, y0, z1); glVertex3d(x1, y0, z0);
  glColorShade(color, -10);
  glVertex3d(x1, y0, z0); glVertex3d(x1, y0, z1); glVertex3d(x1, y1, z1); glVertex3d(x1, y1, z0);
  glColorShade(color, -44);
  glVertex3d(x0, y0, z0); glVertex3d(x0, y1, z0); glVertex3d(x0, y1, z1); glVertex3d(x0, y0, z1);
  glColorShade(color, 4);
  glVertex3d(x0, y0, z1); glVertex3d(x0, y1, z1); glVertex3d(x1, y1, z1); glVertex3d(x1, y0, z1);
  glColorShade(color, -22);
  glVertex3d(x0, y0, z0); glVertex3d(x1, y0, z0); glVertex3d(x1, y1, z0); glVertex3d(x0, y1, z0);
  glEnd();

  glColorShade(color, -56);
  drawCubeEdges(x0, x1, y0, y1, z0, z1);
}

/* ===== 渲染风格：体素 / 边缘 / STL（对齐网页版 drawCubesEdges / drawCubesSTL） ===== */
static const int FAXIS6[6] = {2, 2, 0, 0, 1, 1};
static const int FSIGN6[6] = {1, -1, 1, -1, 1, -1};
static const int FACE_DELTA[6] = {4, -22, -10, -44, 24, -32}; // +Z,-Z,+X,-X,+Y,-Y 与 drawCube 一致
static const int EDGE_PAIRS[12][2] = {
  {0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{2,5},{3,4},{3,5}
};
static int fidx6(int a, int s) {
  if (a == 2) return s > 0 ? 0 : 1;
  if (a == 0) return s > 0 ? 2 : 3;
  return s > 0 ? 4 : 5;
}
static std::string ckey3(int x, int y, int z) {
  return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
}
static std::set<std::string> occupiedSet(const std::vector<RenderCell> &cells) {
  std::set<std::string> s;
  for (const auto &c : cells)
    if (!c.wire) s.insert(ckey3(c.gx, c.gy, c.gz));
  return s;
}
static void exposedFlags(const RenderCell &c, const std::set<std::string> &occ, bool ex[6]) {
  for (int f = 0; f < 6; ++f) {
    int n[3] = {c.gx, c.gy, c.gz};
    n[FAXIS6[f]] += FSIGN6[f];
    ex[f] = occ.find(ckey3(n[0], n[1], n[2])) == occ.end();
  }
}
/* 面上一点：a 轴固定在面平面，其余两轴按 sg 方向，可朝真实棱边内缩 bev */
static void facePoint(const double C[3], int f, const int sg[3], const bool ex[6], double bev, double out[3]) {
  int a = FAXIS6[f];
  out[0] = C[0]; out[1] = C[1]; out[2] = C[2];
  out[a] = C[a] + (FSIGN6[f] > 0 ? 0.5 : -0.5);
  for (int x = 0; x < 3; ++x)
    if (x != a) out[x] = C[x] + sg[x] * (0.5 - (ex[fidx6(x, sg[x])] ? bev : 0.0));
}
static void glFillQuad(const double p[4][3]) {
  glBegin(GL_QUADS);
  for (int i = 0; i < 4; ++i) glVertex3dv(p[i]);
  glEnd();
}
static void glFillTri(const double p[3][3]) {
  glBegin(GL_TRIANGLES);
  for (int i = 0; i < 3; ++i) glVertex3dv(p[i]);
  glEnd();
}
static void drawWireCell(double x, double y, double z, COLORREF col) {
  const double s = 0.48;
  glColorShade(col, 18);
  drawCubeEdges(x - s, x + s, y - s, y + s, z - s, z + s);
}
/* 边缘：只画暴露面（无内部网格线），仅两侧面都朝外的真实棱边画线 */
static void drawCubesEdgesStyle(const std::vector<RenderCell> &cells, const std::set<std::string> &occ) {
  for (const auto &c : cells) {
    double C[3] = {c.x, c.y, c.z};
    COLORREF col = pieceColor(c.piece);
    if (c.wire) { drawWireCell(C[0], C[1], C[2], col); continue; }
    bool ex[6]; exposedFlags(c, occ, ex);
    for (int f = 0; f < 6; ++f) {
      if (!ex[f]) continue;
      int a = FAXIS6[f], a1 = (a + 1) % 3, a2 = (a + 2) % 3;
      const int S1[4] = {1, -1, -1, 1}, S2[4] = {1, 1, -1, -1};
      int sg[3] = {0, 0, 0};
      double p[4][3];
      for (int k = 0; k < 4; ++k) {
        sg[a1] = S1[k]; sg[a2] = S2[k];
        facePoint(C, f, sg, ex, 0.0, p[k]);
      }
      glColorShade(col, (int)(FACE_DELTA[f] * c.f));
      glFillQuad(p);
    }
    glColor3ub(20, 25, 20);
    glBegin(GL_LINES);
    for (int e = 0; e < 12; ++e) {
      int i = EDGE_PAIRS[e][0], j = EDGE_PAIRS[e][1];
      if (!(ex[i] && ex[j])) continue;
      int ai = FAXIS6[i], aj = FAXIS6[j], ak = 3 - ai - aj;
      double p0[3] = {C[0], C[1], C[2]}, p1[3] = {C[0], C[1], C[2]};
      p0[ai] = p1[ai] = C[ai] + (FSIGN6[i] > 0 ? 0.5 : -0.5);
      p0[aj] = p1[aj] = C[aj] + (FSIGN6[j] > 0 ? 0.5 : -0.5);
      p0[ak] = C[ak] - 0.5; p1[ak] = C[ak] + 0.5;
      glVertex3dv(p0); glVertex3dv(p1);
    }
    glEnd();
  }
}
/* STL：整块倒角——暴露面内缩、真实棱边补倒角条、真实角点补三角面，按平均法向着色 */
static void drawCubesSTLStyle(const std::vector<RenderCell> &cells, const std::set<std::string> &occ) {
  const double bev = 0.12;
  for (const auto &c : cells) {
    double C[3] = {c.x, c.y, c.z};
    COLORREF col = pieceColor(c.piece);
    if (c.wire) { drawWireCell(C[0], C[1], C[2], col); continue; }
    bool ex[6]; exposedFlags(c, occ, ex);
    // 暴露面：朝真实棱边方向内缩
    for (int f = 0; f < 6; ++f) {
      if (!ex[f]) continue;
      int a = FAXIS6[f], a1 = (a + 1) % 3, a2 = (a + 2) % 3;
      const int S1[4] = {1, -1, -1, 1}, S2[4] = {1, 1, -1, -1};
      int sg[3] = {0, 0, 0};
      double p[4][3];
      for (int k = 0; k < 4; ++k) {
        sg[a1] = S1[k]; sg[a2] = S2[k];
        facePoint(C, f, sg, ex, bev, p[k]);
      }
      glColorShade(col, (int)(FACE_DELTA[f] * c.f));
      glFillQuad(p);
    }
    // 真实棱边：倒角条
    for (int e = 0; e < 12; ++e) {
      int i = EDGE_PAIRS[e][0], j = EDGE_PAIRS[e][1];
      if (!(ex[i] && ex[j])) continue;
      int ai = FAXIS6[i], aj = FAXIS6[j], ak = 3 - ai - aj;
      int sI1[3] = {0,0,0}, sI2[3] = {0,0,0}, sJ1[3] = {0,0,0}, sJ2[3] = {0,0,0};
      sI1[aj] = FSIGN6[j]; sI1[ak] = 1;  sI2[aj] = FSIGN6[j]; sI2[ak] = -1;
      sJ1[ai] = FSIGN6[i]; sJ1[ak] = 1;  sJ2[ai] = FSIGN6[i]; sJ2[ak] = -1;
      double p[4][3];
      facePoint(C, i, sI1, ex, bev, p[0]);
      facePoint(C, i, sI2, ex, bev, p[1]);
      facePoint(C, j, sJ2, ex, bev, p[2]);
      facePoint(C, j, sJ1, ex, bev, p[3]);
      glColorShade(col, (int)((FACE_DELTA[i] + FACE_DELTA[j]) * 0.5 * c.f));
      glFillQuad(p);
    }
    // 真实角点：三角面
    for (int fx = 0; fx < 2; ++fx) for (int fy = 2; fy < 4; ++fy) for (int fz = 4; fz < 6; ++fz) {
      int tri[3] = {fx, fy, fz};
      if (!(ex[fx] && ex[fy] && ex[fz])) continue;
      double p[3][3];
      for (int t = 0; t < 3; ++t) {
        int f = tri[t], sg[3] = {0, 0, 0};
        for (int ax = 0; ax < 3; ++ax) {
          if (ax == FAXIS6[f]) continue;
          for (int u = 0; u < 3; ++u)
            if (FAXIS6[tri[u]] == ax) { sg[ax] = FSIGN6[tri[u]]; break; }
        }
        facePoint(C, f, sg, ex, bev, p[t]);
      }
      glColorShade(col, (int)((FACE_DELTA[fx] + FACE_DELTA[fy] + FACE_DELTA[fz]) / 3.0 * c.f));
      glFillTri(p);
    }
  }
}
/* 统一入口：按风格分发；体素保持原 drawCube */
static void drawCubesStyled(const std::vector<RenderCell> &cells) {
  if (g_renderStyle == RS_VOXEL) {
    for (const auto &c : cells)
      drawCube(c.x, c.y, c.z, pieceColor(c.piece), c.wire);
    return;
  }
  std::set<std::string> occ = occupiedSet(cells);
  if (g_renderStyle == RS_EDGES) drawCubesEdgesStyle(cells, occ);
  else drawCubesSTLStyle(cells, occ);
}

/* 判断 2x2xL 网格中某格是否实心：编辑格由码位决定，其余固定实心 */
static bool cellIsSolidAt(int x, int y, int z, int code, int ext) {
  if (x < 0 || x > 1 || y < 0 || y > 1 || z < 0) return false;
  for (int i = 0; i < 12; ++i)
    if (EDIT_IP[i][0] == x && EDIT_IP[i][1] == y && EDIT_IP[i][2] + ext == z)
      return (code & (1 << i)) == 0;
  return true;
}

/* 暴露面斜线高亮：保留原色的淡色底 + 45° 深色斜线 + 黑边 */
static void drawHatchFace(const double q[4][3], COLORREF col) {
  glColorShade(col, 12);
  glBegin(GL_QUADS);
  for (int i = 0; i < 4; ++i) glVertex3dv(q[i]);
  glEnd();

  auto bilinear = [&](double u, double v, double out[3]) {
    for (int k = 0; k < 3; ++k)
      out[k] = (1-u)*(1-v)*q[0][k] + u*(1-v)*q[1][k] + u*v*q[2][k] + (1-u)*v*q[3][k];
  };
  glColor3ub(28, 34, 50);
  glBegin(GL_LINES);
  const double step = 0.2;
  for (double c = step; c < 2.0 - step * 0.5; c += step) {
    double ua, va, ub, vb;
    if (c <= 1.0) { ua = c; va = 0; ub = 0; vb = c; }
    else { ua = 1; va = c - 1; ub = c - 1; vb = 1; }
    double a[3], b[3];
    bilinear(ua, va, a); bilinear(ub, vb, b);
    glVertex3dv(a); glVertex3dv(b);
  }
  glEnd();

  glColor3ub(8, 12, 18);
  glBegin(GL_LINE_LOOP);
  for (int i = 0; i < 4; ++i) glVertex3dv(q[i]);
  glEnd();
}

/* 画一个格子的暴露面：相邻有方块遮住的面不画；空位用灰色 */
static void drawHoverCell(double cx, double cy, double cz, int editIdx,
                          int code, int ext, int length, COLORREF col) {
  int gx = EDIT_IP[editIdx][0];
  int gy = EDIT_IP[editIdx][1];
  int gz = EDIT_IP[editIdx][2] + ext;
  const double s = 0.485;
  double x0 = gx - s - cx, x1 = gx + s - cx;
  double y0 = gy - s - cy, y1 = gy + s - cy;
  double z0 = gz - s - cz, z1 = gz + s - cz;

  static const int DIR[6][3] = {
    {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
  };
  double q[6][4][3] = {};
  // +Z
  q[0][0][0]=x0;q[0][0][1]=y0;q[0][0][2]=z1; q[0][1][0]=x1;q[0][1][1]=y0;q[0][1][2]=z1;
  q[0][2][0]=x1;q[0][2][1]=y1;q[0][2][2]=z1; q[0][3][0]=x0;q[0][3][1]=y1;q[0][3][2]=z1;
  // -Z
  q[1][0][0]=x0;q[1][0][1]=y0;q[1][0][2]=z0; q[1][1][0]=x1;q[1][1][1]=y0;q[1][1][2]=z0;
  q[1][2][0]=x1;q[1][2][1]=y1;q[1][2][2]=z0; q[1][3][0]=x0;q[1][3][1]=y1;q[1][3][2]=z0;
  // +X
  q[2][0][0]=x1;q[2][0][1]=y0;q[2][0][2]=z0; q[2][1][0]=x1;q[2][1][1]=y1;q[2][1][2]=z0;
  q[2][2][0]=x1;q[2][2][1]=y1;q[2][2][2]=z1; q[2][3][0]=x1;q[2][3][1]=y0;q[2][3][2]=z1;
  // -X
  q[3][0][0]=x0;q[3][0][1]=y0;q[3][0][2]=z0; q[3][1][0]=x0;q[3][1][1]=y1;q[3][1][2]=z0;
  q[3][2][0]=x0;q[3][2][1]=y1;q[3][2][2]=z1; q[3][3][0]=x0;q[3][3][1]=y0;q[3][3][2]=z1;
  // +Y
  q[4][0][0]=x0;q[4][0][1]=y1;q[4][0][2]=z0; q[4][1][0]=x1;q[4][1][1]=y1;q[4][1][2]=z0;
  q[4][2][0]=x1;q[4][2][1]=y1;q[4][2][2]=z1; q[4][3][0]=x0;q[4][3][1]=y1;q[4][3][2]=z1;
  // -Y
  q[5][0][0]=x0;q[5][0][1]=y0;q[5][0][2]=z0; q[5][1][0]=x1;q[5][1][1]=y0;q[5][1][2]=z0;
  q[5][2][0]=x1;q[5][2][1]=y0;q[5][2][2]=z1; q[5][3][0]=x0;q[5][3][1]=y0;q[5][3][2]=z1;

  glDepthFunc(GL_LEQUAL);
  for (int f = 0; f < 6; ++f) {
    int nx = gx + DIR[f][0], ny = gy + DIR[f][1], nz = gz + DIR[f][2];
    bool exposed = true;
    if (nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1 && nz >= 0 && nz < length)
      exposed = !cellIsSolidAt(nx, ny, nz, code, ext);
    if (exposed) drawHatchFace(q[f], col);
  }
  glDepthFunc(GL_LESS);
}

/* 线段勾勒 X/Y/Z 字母（在轴端附近） */
static void strokeAxisGlyph(char ch) {
  const double s = 0.17;
  auto seg = [&](double x1, double y1, double x2, double y2) {
    glVertex3d(x1, y1, 0); glVertex3d(x2, y2, 0);
  };
  glBegin(GL_LINES);
  if (ch == 'X') { seg(-s,-s,s,s); seg(-s,s,s,-s); }
  else if (ch == 'Y') { seg(-s,s,0,0); seg(s,s,0,0); seg(0,0,0,-s); }
  else { seg(-s,s,s,s); seg(s,s,-s,-s); seg(-s,-s,s,-s); }
  glEnd();
}

/* 左下角三维坐标轴指示器（X红 Y绿 Z蓝），随视角旋转 */
static void drawAxisGizmo(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  (void)rc;
  const int side = 78;
  glViewport(16, 16, side, side);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1.65, 1.65, -1.65, 1.65, -10.0, 10.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glRotated(g_pitch, 1.0, 0.0, 0.0);
  glRotated(g_yaw * 57.29577951308232, 0.0, 1.0, 0.0);
  glDisable(GL_DEPTH_TEST);

  struct Axis { double e[3]; double labelPos[3]; COLORREF c; char ch; };
  Axis axes[3] = {
    {{1.15,0,0}, {1.38,0,0}, RGB(222, 74, 74), 'X'},
    {{0,1.15,0}, {0,1.38,0}, RGB(62, 178, 96), 'Y'},
    {{0,0,1.15}, {0,0,1.38}, RGB(64, 132, 222), 'Z'}
  };
  for (const auto &a : axes) {
    glColor3ub(GetRValue(a.c), GetGValue(a.c), GetBValue(a.c));
    glBegin(GL_LINES);
    glVertex3d(0,0,0); glVertex3d(a.e[0], a.e[1], a.e[2]);
    glEnd();
    glPushMatrix();
    glTranslated(a.labelPos[0], a.labelPos[1], a.labelPos[2]);
    strokeAxisGlyph(a.ch);
    glPopMatrix();
  }
  glEnable(GL_DEPTH_TEST);
}

void drawOpenGLVoxelView(HWND hwnd) {
  if (!g_glDc || !g_glRc) return;
  wglMakeCurrent(g_glDc, g_glRc);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // 与网页版一致：finalRemoval 动作完成后，柱子进入 removed 状态；
  // 往回播放时 rebuildToStep 会从初始状态重放，自动恢复已移除柱子。
  bool removed[7] = {false};
  for (int si = 0; si < g_currentStep && si < (int)g_moves.size(); ++si) {
    if (g_moves[si].finalRemoval)
      for (int g : g_moves[si].group) if (g >= 1 && g <= 6) removed[g] = true;
  }
  const LubanMove *animMove = nullptr;
  std::set<int> animGroup;
  double animT = animationProgress();
  if (g_animating && g_animMoveIndex >= 0 && g_animMoveIndex < (int)g_moves.size()) {
    animMove = &g_moves[g_animMoveIndex];
    animGroup.insert(animMove->group.begin(), animMove->group.end());
    if (g_animSign < 0 && animMove->finalRemoval) {
      for (int g : animMove->group) if (g >= 1 && g <= 6) removed[g] = false;
    }
  }

  std::vector<RenderCell> cells;
  if (g_pieces.empty()) {
    int length = selectedLength();
    for (const auto &cell : previewCellsFromCode(editCode(g_activePiece), length)) {
      cells.push_back({g_activePiece + 1, (double)cell.x, (double)cell.y, (double)cell.z,
                       cell.x, cell.y, cell.z, false, 1.0});
    }
  } else {
    for (const auto &piece : g_pieces) {
      int idx = piece.piece;
      if (idx >= 1 && idx <= 6 && (g_pieceDisplayMode[idx - 1] == 2 || removed[idx])) continue;
      for (const auto &cell : piece.cells) {
        double x = (double)cell.x;
        double y = (double)cell.y;
        double z = (double)cell.z;
        if (animMove && animGroup.count(idx)) {
          double amount = animT * (double)g_animSign;
          if (animMove->rotate) {
            double angle = amount * (double)animMove->rotationDir * 1.5707963267948966;
            double cs = std::cos(angle);
            double sn = std::sin(angle);
            switch (animMove->rotationAxis) {
              case 'x': {
                double ry = y - animMove->pivot.y;
                double rz = z - animMove->pivot.z;
                y = ry * cs - rz * sn + animMove->pivot.y;
                z = ry * sn + rz * cs + animMove->pivot.z;
                break;
              }
              case 'z': {
                double rx = x - animMove->pivot.x;
                double ry = y - animMove->pivot.y;
                x = rx * cs - ry * sn + animMove->pivot.x;
                y = rx * sn + ry * cs + animMove->pivot.y;
                break;
              }
              default: { // 'y'
                double rx = x - animMove->pivot.x;
                double rz = z - animMove->pivot.z;
                x = rx * cs + rz * sn + animMove->pivot.x;
                z = -rx * sn + rz * cs + animMove->pivot.z;
                break;
              }
            }
          } else {
            x += animMove->dx * animMove->steps * amount;
            y += animMove->dy * animMove->steps * amount;
            z += animMove->dz * animMove->steps * amount;
          }
        }
        bool wf = idx >= 1 && idx <= 6 && g_pieceDisplayMode[idx - 1] == 1;
        int gx = (int)std::lround(x), gy = (int)std::lround(y), gz = (int)std::lround(z);
        cells.push_back({idx, x, y, z, gx, gy, gz, wf, 1.0});
      }
    }
  }

  if (cells.empty()) {
    setProjection(hwnd, 8.0);
    glLoadIdentity();
    glRotated(g_pitch, 1.0, 0.0, 0.0);
    glRotated(g_yaw * 57.29577951308232, 0.0, 1.0, 0.0);
    drawAxisGizmo(hwnd);
    SwapBuffers(g_glDc);
    return;
  }

  double minX = cells[0].x, maxX = cells[0].x;
  double minY = cells[0].y, maxY = cells[0].y;
  double minZ = cells[0].z, maxZ = cells[0].z;
  for (const auto &entry : cells) {
    minX = std::min(minX, entry.x); maxX = std::max(maxX, entry.x);
    minY = std::min(minY, entry.y); maxY = std::max(maxY, entry.y);
    minZ = std::min(minZ, entry.z); maxZ = std::max(maxZ, entry.z);
  }
  double cx = (minX + maxX) * 0.5;
  double cy = (minY + maxY) * 0.5;
  double cz = (minZ + maxZ) * 0.5;
  int viewLength = g_pieces.empty() ? selectedLength() : g_lengthValue;
  double span = std::max({(double)viewLength, (double)(maxX - minX + 1), (double)(maxY - minY + 1), (double)(maxZ - minZ + 1)});
  setProjection(hwnd, span);

  glLoadIdentity();
  glRotated(g_pitch, 1.0, 0.0, 0.0);
  glRotated(g_yaw * 57.29577951308232, 0.0, 1.0, 0.0);

  std::vector<RenderCell> centered;
  centered.reserve(cells.size());
  for (const auto &entry : cells)
    centered.push_back({entry.piece, entry.x - cx, entry.y - cy, entry.z - cz,
                        entry.gx, entry.gy, entry.gz, entry.wire, entry.f});
  drawCubesStyled(centered);

  // 活动块悬停高亮：仅编辑态显示；实心=组色、挖空=灰色，均只画暴露面
  if (g_pieces.empty() && g_hoverIdx >= 0 && g_hoverIdx < 12) {
    int code = editCode(g_activePiece);
    int length = selectedLength();
    int ext = extLenForPreview(length);
    COLORREF col = (code & (1 << g_hoverIdx)) ? RGB(138, 144, 137) : actColor(g_hoverIdx);
    drawHoverCell(cx, cy, cz, g_hoverIdx, code, ext, length, col);
  }
  drawAxisGizmo(hwnd);
  SwapBuffers(g_glDc);
}

void showResult(const LubanSolveSummary &result) {
  stopPlaybackAnimation();
  g_moves = result.moves;
  g_initialPieces = lockedDisplayPieces(result);
  g_pieces = g_initialPieces;
  g_stages = makePlaybackStages(g_moves);
  g_lengthValue = result.length;
  g_currentStep = 0;

  std::wstring summary;
  summary += fmt(L"目标：%s\r\n", toWide(result.targetName).c_str());
  summary += fmt(L"本次用时：%.2f 秒；核心 %.2f ms；节点：%d；候选：%d",
                 result.elapsedMs / 1000.0, result.elapsedMs, result.searchNodes, result.checkedAssemblies);
  if (result.skippedDuplicates > 0)
    summary += fmt(L"（去重跳过 %d，实际验证 %d 个唯一装配）", result.skippedDuplicates,
                   result.checkedAssemblies - result.skippedDuplicates);
  summary += L"\r\n";
  summary += fmt(L"可拼装：%s；可拆解：%s；旋转锁：%s\r\n",
                 result.assemblyOk ? L"是" : L"否",
                 result.disassemblable ? L"是" : L"否",
                 result.rotationLock ? L"是" : L"否");
  if (result.rotationLock) {
    summary += fmt(L"旋转柱：%d；轴心：%d,%d,%d；预旋转：%s\r\n",
                   result.rotatePiece, result.pivotX, result.pivotY, result.pivotZ,
                   toWide(result.preRotation).c_str());
  }
  summary += fmt(L"难度：第一阶段 %d；最大阶段 %d；阶段数 %d；总动作 %d",
                 result.firstLevel, result.maxStage, result.exitCount, result.totalMoves);
  if (!result.error.empty()) summary += L"\r\n提示：" + toWide(result.error);
  SetWindowTextW(g_summary, summary.c_str());

  std::wstring allSteps;
  for (size_t i = 0; i < g_stages.size(); ++i) {
    std::wstring line = fmt(L"%d. %s\r\n", (int)i + 1, g_stages[i].text.c_str());
    allSteps += line;
  }
  SetWindowTextW(g_steps, allSteps.c_str());
  updateButtons();
  invalidateView();
}

std::wstring trimLine(const std::wstring &line) {
  size_t begin = 0;
  while (begin < line.size() && iswspace(line[begin])) ++begin;
  size_t end = line.size();
  while (end > begin && iswspace(line[end - 1])) --end;
  return line.substr(begin, end - begin);
}

bool readWideLine(FILE *f, std::wstring &out) {
  wchar_t buffer[4096];
  if (!fgetws(buffer, 4096, f)) return false;
  out = trimLine(buffer);
  return true;
}

void setLengthControlValue(int length) {
  int sel = 0;
  if (length == 8) sel = 1;
  else if (length == 10) sel = 2;
  else if (length == 12) sel = 3;
  SendMessage(g_length, CB_SETCURSEL, (WPARAM)sel, 0);
  g_lengthValue = (length == 8 || length == 10 || length == 12) ? length : 6;
}

void refreshLoadedStepList() {
  std::wstring allSteps;
  for (size_t i = 0; i < g_stages.size(); ++i) {
    std::wstring line = fmt(L"%d. %s\r\n", (int)i + 1, g_stages[i].text.c_str());
    allSteps += line;
  }
  SetWindowTextW(g_steps, allSteps.c_str());
}

void applyLoadedProject(const std::vector<int> &codes, int length,
                        const std::vector<LubanPieceView> &pieces,
                        const std::vector<LubanMove> &moves,
                        int currentStep) {
  stopPlaybackAnimation();
  if (g_main) {
    KillTimer(g_main, IDC_TIMER);
    KillTimer(g_main, IDC_SOLVE_TIMER);
  }
  g_solving = false;
  g_solveStartTick = 0;
  setLengthControlValue(length);
  for (int i = 0; i < 6; ++i) {
    int code = i < (int)codes.size() ? codes[i] : 0;
    setEditCode(i, code);
    g_pieceDisplayMode[i] = 0;
    g_pieceVisible[i] = true;
    SendMessage(g_showChecks[i], BM_SETCHECK, BST_CHECKED, 0);
  }
  updatePieceDisplayButtons();
  updateCellButtonsFromCode();

  g_initialPieces = pieces;
  g_pieces = pieces;
  g_moves = moves;
  g_stages = makePlaybackStages(g_moves);
  refreshLoadedStepList();
  g_currentStep = 0;
  if (!g_initialPieces.empty() && !g_moves.empty()) {
    rebuildToStep(std::max(0, std::min(currentStep, (int)g_moves.size())));
  } else {
    invalidateView();
  }

  std::wstring summary = L"已打开保存文件。\r\n";
  summary += fmt(L"柱长：2x2x%d；编码：", g_lengthValue);
  for (int i = 0; i < 6; ++i) {
    if (i) summary += L", ";
    summary += std::to_wstring(i < (int)codes.size() ? codes[i] : 0);
  }
  if (!g_initialPieces.empty()) {
    summary += fmt(L"\r\n已还原拼装数据：%d 根柱子；拆解小步：%d",
                   (int)g_initialPieces.size(), (int)g_moves.size());
  } else {
    summary += L"\r\n此文件只有输入编码，请点击“求解”重新生成拼装和拆解数据。";
  }
  SetWindowTextW(g_summary, summary.c_str());
  updateButtons();
}

bool saveProjectFile(const wchar_t *path) {
  FILE *f = _wfopen(path, L"w, ccs=UTF-8");
  if (!f) return false;

  std::vector<int> codes;
  readCodes(codes);
  fwprintf(f, L"LUBAN_WIN32_GUI_SAVE_V2\n");
  fwprintf(f, L"length %d\n", selectedLength());
  fwprintf(f, L"codes");
  for (int code : codes) fwprintf(f, L" %d", code);
  fwprintf(f, L"\n");
  fwprintf(f, L"currentStep %d\n", g_currentStep);

  fwprintf(f, L"pieces %d\n", (int)g_initialPieces.size());
  for (const auto &piece : g_initialPieces) {
    fwprintf(f, L"piece %d %d %d %d %d\n", piece.piece, piece.code, piece.slot,
             piece.rotationIndex, (int)piece.cells.size());
    for (const auto &c : piece.cells) {
      fwprintf(f, L"cell %d %d %d\n", c.x, c.y, c.z);
    }
  }

  fwprintf(f, L"moves %d\n", (int)g_moves.size());
  for (int i = 0; i < (int)g_moves.size(); ++i) {
    const auto &m = g_moves[i];
    std::wstring dir = m.direction.empty() ? L"-" : toWide(m.direction);
    fwprintf(f, L"move %d %d", i, (int)m.group.size());
    for (int g : m.group) fwprintf(f, L" %d", g);
    fwprintf(f, L" %s %d %d %d %d %d %d %d %d %d %d %d\n",
             dir.c_str(), m.dx, m.dy, m.dz, m.steps, m.exit ? 1 : 0,
             m.finalRemoval ? 1 : 0, m.rotate ? 1 : 0, m.pivot.x, m.pivot.y,
             m.pivot.z, m.rotationDir);
    fwprintf(f, L"text %s\n", toWide(m.text).c_str());
  }
  fclose(f);
  return true;
}

bool loadProjectFile(const wchar_t *path) {
  FILE *f = _wfopen(path, L"r, ccs=UTF-8");
  if (!f) return false;

  std::wstring line;
  if (!readWideLine(f, line)) {
    fclose(f);
    return false;
  }

  std::vector<int> codes(6, 0);
  int length = 6;
  int currentStep = 0;
  std::vector<LubanPieceView> pieces;
  std::vector<LubanMove> moves;

  if (line != L"LUBAN_WIN32_GUI_SAVE_V2") {
    length = _wtoi(line.c_str());
    for (int i = 0; i < 6; ++i) {
      if (!readWideLine(f, line)) break;
      int code = _wtoi(line.c_str());
      if (code >= 0 && code <= 4095) codes[i] = code;
    }
    fclose(f);
    applyLoadedProject(codes, length, pieces, moves, 0);
    return true;
  }

  while (readWideLine(f, line)) {
    if (line.empty()) continue;
    std::wstringstream ss(line);
    std::wstring tag;
    ss >> tag;
    if (tag == L"length") {
      ss >> length;
    } else if (tag == L"codes") {
      for (int i = 0; i < 6; ++i) {
        int code = 0;
        if (ss >> code) codes[i] = std::max(0, std::min(4095, code));
      }
    } else if (tag == L"currentStep") {
      ss >> currentStep;
    } else if (tag == L"pieces") {
      int count = 0;
      ss >> count;
      pieces.clear();
      for (int i = 0; i < count; ++i) {
        std::wstring header;
        if (!readWideLine(f, header)) break;
        std::wstringstream hs(header);
        std::wstring pieceTag;
        LubanPieceView piece;
        int cellCount = 0;
        hs >> pieceTag >> piece.piece >> piece.code >> piece.slot >> piece.rotationIndex >> cellCount;
        if (pieceTag != L"piece") break;
        for (int c = 0; c < cellCount; ++c) {
          std::wstring cellLine;
          if (!readWideLine(f, cellLine)) break;
          std::wstringstream cs(cellLine);
          std::wstring cellTag;
          LubanCell cell;
          cs >> cellTag >> cell.x >> cell.y >> cell.z;
          if (cellTag == L"cell") piece.cells.push_back(cell);
        }
        pieces.push_back(piece);
      }
    } else if (tag == L"moves") {
      int count = 0;
      ss >> count;
      moves.clear();
      for (int i = 0; i < count; ++i) {
        std::wstring moveLine;
        if (!readWideLine(f, moveLine)) break;
        std::wstringstream ms(moveLine);
        std::wstring moveTag, dir;
        int index = 0, groupCount = 0, exit = 0, finalRemoval = 0, rotate = 0;
        LubanMove move;
        ms >> moveTag >> index >> groupCount;
        if (moveTag != L"move") break;
        for (int g = 0; g < groupCount; ++g) {
          int pieceId = 0;
          ms >> pieceId;
          move.group.push_back(pieceId);
        }
        ms >> dir >> move.dx >> move.dy >> move.dz >> move.steps >> exit >> finalRemoval >> rotate
           >> move.pivot.x >> move.pivot.y >> move.pivot.z >> move.rotationDir;
        move.direction = dir == L"-" ? "" : toUtf8(dir);
        move.exit = exit != 0;
        move.finalRemoval = finalRemoval != 0;
        move.rotate = rotate != 0;

        std::wstring textLine;
        if (readWideLine(f, textLine) && textLine.rfind(L"text ", 0) == 0) {
          move.text = toUtf8(textLine.substr(5));
        }
        moves.push_back(move);
      }
    }
  }

  fclose(f);
  applyLoadedProject(codes, length, pieces, moves, currentStep);
  return true;
}

double currentSolveSeconds() {
  if (!g_solveStartTick) return 0.0;
  return (double)(GetTickCount() - g_solveStartTick) / 1000.0;
}

void updateSolveTimerText() {
  double seconds = currentSolveSeconds();
  SetWindowTextW(g_status, fmt(L"正在求解... %.1f 秒", seconds).c_str());
  SetWindowTextW(g_summary,
                 fmt(L"正在求解...\r\n\r\n已用时：%.1f 秒\r\n\r\n程序正在搜索拼装、拆解路径和旋转锁可能性，请稍候。", seconds).c_str());
}

DWORD WINAPI solveThreadProc(LPVOID param) {
  SolveRequest *request = static_cast<SolveRequest *>(param);
  LubanSolveSummary *result = new LubanSolveSummary(solveLubanLock(request->codes, request->length));
  PostMessageW(request->hwnd, WM_SOLVE_DONE, 0, (LPARAM)result);
  delete request;
  return 0;
}

void runSolve() {
  if (g_solving) return;
  std::vector<int> codes;
  if (!readCodes(codes)) {
    MessageBoxW(g_main, L"请输入 6 个 0-4095 范围内的柱子编码。", L"输入错误", MB_ICONWARNING);
    return;
  }
  stopPlaybackAnimation();
  KillTimer(g_main, IDC_TIMER);
  g_initialPieces.clear();
  g_pieces.clear();
  g_moves.clear();
  g_stages.clear();
  g_currentStep = 0;
  SetWindowTextW(g_steps, L"");
  invalidateView();
  g_solving = true;
  g_solveStartTick = GetTickCount();
  g_lengthValue = selectedLength();
  updateButtons();
  updateSolveTimerText();
  UpdateWindow(g_summary);
  UpdateWindow(g_status);
  SetCursor(LoadCursor(nullptr, IDC_WAIT));
  SetTimer(g_main, IDC_SOLVE_TIMER, 250, nullptr);

  SolveRequest *request = new SolveRequest;
  request->hwnd = g_main;
  request->codes = codes;
  request->length = g_lengthValue;
  HANDLE thread = CreateThread(nullptr, 0, solveThreadProc, request, 0, nullptr);
  if (thread) {
    CloseHandle(thread);
  } else {
    delete request;
    KillTimer(g_main, IDC_SOLVE_TIMER);
    g_solving = false;
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    MessageBoxW(g_main, L"无法启动求解线程。", L"运行错误", MB_ICONERROR);
    updateButtons();
  }
}

void layout(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  int margin = 10;
  int leftW = 280;
  int rightW = 360;
  int topH = h - 42;
  int centerX = leftW + margin * 2;
  int centerW = w - leftW - rightW - margin * 4;

  MoveWindow(g_view, centerX, margin + 32, centerW, h - margin * 2 - 32, TRUE);

  int rightX = w - rightW;
  int rightCtrlW = rightW - 20;
  int rightY = margin + 34;
  MoveWindow(g_summary, rightX, rightY, rightCtrlW, 176, TRUE);
  rightY += 190;
  MoveWindow(g_status, rightX, rightY, rightCtrlW, 26, TRUE);
  rightY += 34;

  int modeBtnW = (rightCtrlW - 16) / 3;
  for (int i = 0; i < 6; ++i) {
    int row = i / 3;
    int col = i % 3;
    MoveWindow(g_viewModeButtons[i], rightX + col * (modeBtnW + 8), rightY + row * 34, modeBtnW, 30, TRUE);
  }
  rightY += 72;

  int bw = (rightCtrlW - 10) / 2;
  MoveWindow(g_buttons[0], rightX, rightY, bw, 30, TRUE);
  MoveWindow(g_buttons[1], rightX + bw + 10, rightY, bw, 30, TRUE);
  rightY += 34;
  MoveWindow(g_buttons[2], rightX, rightY, bw, 30, TRUE);
  MoveWindow(g_buttons[3], rightX + bw + 10, rightY, bw, 30, TRUE);
  rightY += 34;
  MoveWindow(g_buttons[4], rightX, rightY, rightCtrlW, 30, TRUE);
  rightY += 40;

  int editorTitleY = 48;
  int pieceButtonY = 76;
  MoveWindow(g_editorTitle, 20, editorTitleY, 220, 22, TRUE);
  for (int i = 0; i < 6; ++i) {
    int row = i / 3;
    int col = i % 3;
    MoveWindow(g_pieceButtons[i], 20 + col * 78, pieceButtonY + row * 32, 72, 28, TRUE);
  }
  MoveWindow(g_actTitle, 20, 142, 250, 22, TRUE);
  for (int pos = 0; pos < 12; ++pos) {
    int row = pos / 2, col = pos % 2;
    MoveWindow(g_actButtons[pos], 20 + col * 130, 164 + row * 25, 125, 23, TRUE);
  }
  MoveWindow(g_rstyleLabel, 20, 316, 60, 22, TRUE);
  for (int i = 0; i < 3; ++i)
    MoveWindow(g_rstyleButtons[i], 82 + i * 58, 314, 54, 25, TRUE);

  int codeTitleY = 342;
  int y = 368;
  MoveWindow(g_codeTitle, 20, codeTitleY, 180, 22, TRUE);
  for (int i = 0; i < 6; ++i) {
    MoveWindow(g_codeLabels[i], 20, y + i * 38 + 2, 60, 22, TRUE);
    MoveWindow(g_edits[i], 86, y + i * 38, 110, 25, TRUE);
    MoveWindow(g_showChecks[i], 204, y + i * 38, 58, 25, TRUE);
  }
  MoveWindow(g_lengthLabel, 20, y + 6 * 38 + 2, 60, 22, TRUE);
  MoveWindow(g_length, 86, y + 6 * 38, 110, 200, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_SOLVE), 20, y + 7 * 38, 110, 30, TRUE);
  MoveWindow(GetDlgItem(hwnd, IDC_INPUT_RESET), 140, y + 7 * 38, 110, 30, TRUE);
  MoveWindow(g_steps, rightX, rightY, rightCtrlW, std::max(120, h - rightY - margin - 10), TRUE);
  InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      setupOpenGL(hwnd);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      drawOpenGLVoxelView(hwnd);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_SIZE:
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_LBUTTONDOWN:
      g_dragging = true;
      g_lastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      SetCapture(hwnd);
      return 0;
    case WM_MOUSEMOVE:
      if (g_dragging) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        g_yaw += (p.x - g_lastMouse.x) * 0.012;
        g_pitch += (p.y - g_lastMouse.y) * 0.18;
        if (g_pitch > 360.0 || g_pitch < -360.0) g_pitch = std::fmod(g_pitch, 360.0);
        g_lastMouse = p;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      g_dragging = false;
      ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL:
      if (GET_WHEEL_DELTA_WPARAM(wp) > 0) g_zoom *= 1.12;
      else g_zoom /= 1.12;
      g_zoom = std::max(0.35, std::min(4.0, g_zoom));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_DESTROY:
      cleanupOpenGL();
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_main = hwnd;
      g_editorTitle = makeControl(L"STATIC", L"选择柱子（1-6）", SS_LEFT, 20, 48, 220, 22, hwnd, 0);
      for (int i = 0; i < 6; ++i) {
        int row = i / 3;
        int col = i % 3;
        g_pieceButtons[i] = makeControl(L"BUTTON", fmt(L"柱子 %d", i + 1).c_str(), BS_PUSHBUTTON,
                                        20 + col * 78, 76 + row * 32, 72, 28,
                                        hwnd, IDC_PIECE_BASE + i);
      }
      updatePieceButtons();
      g_actTitle = makeControl(L"STATIC", L"活动块（3D 高度顺序 · 紫块最下）", SS_LEFT, 20, 142, 250, 22, hwnd, 0);
      setFont(g_actTitle, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
      for (int pos = 0; pos < 12; ++pos) {
        int row = pos / 2, col = pos % 2;
        g_actButtons[pos] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                            20 + col * 130, 164 + row * 25, 125, 23,
                                            hwnd, (HMENU)(INT_PTR)(IDC_ACT_BASE + pos), g_inst, nullptr);
        setFont(g_actButtons[pos], g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        g_actOldProc[pos] = (WNDPROC)SetWindowLongPtr(g_actButtons[pos], GWLP_WNDPROC, (LONG_PTR)ActBtnProc);
      }

      g_codeTitle = makeControl(L"STATIC", L"编码输入", SS_LEFT, 20, 342, 180, 22, hwnd, 0);
      for (int i = 0; i < 6; ++i) {
        g_codeLabels[i] = makeControl(L"STATIC", fmt(L"柱子 %d", i + 1).c_str(), SS_LEFT, 20, 370 + i * 38, 60, 22, hwnd, 0);
        int defaults[6] = {733, 669, 1004, 366, 99, 1023};
        g_edits[i] = makeControl(L"EDIT", std::to_wstring(defaults[i]).c_str(), WS_BORDER | ES_NUMBER, 86, 368 + i * 38, 110, 25, hwnd, IDC_CODE_BASE + i);
        g_showChecks[i] = makeControl(L"BUTTON", L"显示", BS_AUTOCHECKBOX, 204, 368 + i * 38, 58, 25, hwnd, IDC_SHOW_BASE + i);
        SendMessage(g_showChecks[i], BM_SETCHECK, BST_CHECKED, 0);
      }
      g_lengthLabel = makeControl(L"STATIC", L"柱长", SS_LEFT, 20, 598, 60, 22, hwnd, 0);
      g_length = makeControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 86, 596, 110, 200, hwnd, IDC_LENGTH);
      for (auto text : {L"6", L"8", L"10", L"12"}) SendMessageW(g_length, CB_ADDSTRING, 0, (LPARAM)text);
      SendMessage(g_length, CB_SETCURSEL, 0, 0);
      makeControl(L"BUTTON", L"求解", BS_PUSHBUTTON, 20, 634, 110, 30, hwnd, IDC_SOLVE);
      makeControl(L"BUTTON", L"重置输入", BS_PUSHBUTTON, 140, 634, 110, 30, hwnd, IDC_INPUT_RESET);
      updateCellButtonsFromCode();

      g_view = CreateWindowExW(0, L"LubanVoxelView", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 240, 10, 700, 620, hwnd, nullptr, g_inst, nullptr);
      g_rstyleLabel = makeControl(L"STATIC", L"渲染风格", SS_LEFT, 250, 44, 62, 22, hwnd, 0);
      const wchar_t *rsNames[3] = {L"体素", L"边缘", L"STL"};
      for (int i = 0; i < 3; ++i) {
        g_rstyleButtons[i] = makeControl(L"BUTTON", rsNames[i], BS_AUTOCHECKBOX | BS_PUSHLIKE,
                                         314 + i * 58, 41, 54, 25, hwnd, IDC_RSTYLE_BASE + i);
      }
      loadRenderStyle();
      syncRenderStyleButtons();
      for (int i = 0; i < 6; ++i) {
        g_viewModeButtons[i] = makeControl(L"BUTTON", fmt(L"柱子%d", i + 1).c_str(), BS_PUSHBUTTON,
                                           240 + i * 94, 636, 86, 26, hwnd, IDC_VIEWMODE_BASE + i);
      }
      updatePieceDisplayButtons();
      g_status = makeControl(L"STATIC", L"步骤 0 / 0", SS_CENTER, 240, 640, 700, 24, hwnd, 0);
      const wchar_t *btns[5] = {L"重置", L"上一小步", L"下一小步", L"自动拆解", L"自动拼装"};
      int ids[5] = {IDC_RESET, IDC_PREV, IDC_NEXT, IDC_AUTO_DIS, IDC_AUTO_ASM};
      for (int i = 0; i < 5; ++i) g_buttons[i] = makeControl(L"BUTTON", btns[i], BS_PUSHBUTTON, 240 + i * 94, 668, 86, 28, hwnd, ids[i]);

      g_summary = makeControl(L"EDIT", welcomeText().c_str(), WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 970, 34, 360, 148, hwnd, 0);
      g_steps = makeControl(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 970, 224, 360, 450, hwnd, 0);
      setFont(g_summary, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
      setFont(g_steps, g_uiFont ? g_uiFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
      setFont(g_status, g_bigFont ? g_bigFont : g_uiFont);
      for (int i = 0; i < 6; ++i) setFont(g_viewModeButtons[i], g_bigFont ? g_bigFont : g_uiFont);
      for (int i = 0; i < 5; ++i) setFont(g_buttons[i], g_bigFont ? g_bigFont : g_uiFont);
      layout(hwnd);
      updateButtons();
      return 0;
    }
    case WM_ERASEBKGND:
      paintMainBackground(hwnd, (HDC)wp);
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      paintMainBackground(hwnd, hdc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      HDC hdc = (HDC)wp;
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, RGB(32, 39, 49));
      return (LRESULT)g_panelBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = (HDC)wp;
      SetBkColor(hdc, RGB(255, 255, 255));
      SetTextColor(hdc, RGB(20, 24, 30));
      return (LRESULT)g_editBrush;
    }
    case WM_DRAWITEM:
      drawActButton(reinterpret_cast<DRAWITEMSTRUCT *>(lp));
      return TRUE;
    case WM_SIZE:
      layout(hwnd);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_FILE_OPEN: {
          wchar_t path[MAX_PATH] = L"";
          OPENFILENAMEW ofn = {sizeof(ofn), hwnd, 0, L"鲁班锁拼图 (*.lb)\0*.lb\0所有文件 (*.*)\0*.*\0",
                               0, 0, 0, path, MAX_PATH, 0, 0, 0, L"打开拼图",
                               OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR};
          if (GetOpenFileNameW(&ofn)) {
            if (!loadProjectFile(path)) {
              MessageBoxW(hwnd, L"打开失败：文件格式不正确或无法读取。", L"打开拼图", MB_ICONWARNING);
            }
          }
          return 0;
        }
        case ID_FILE_SAVE:
        case ID_FILE_SAVEAS: {
          wchar_t path[MAX_PATH] = L"untitled.lb";
          OPENFILENAMEW ofn = {sizeof(ofn), hwnd, 0, L"鲁班锁拼图 (*.lb)\0*.lb\0所有文件 (*.*)\0*.*\0",
                               0, 0, 0, path, MAX_PATH, 0, 0, 0, L"保存拼图",
                               OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR};
          if (GetSaveFileNameW(&ofn)) {
            if (!saveProjectFile(path)) {
              MessageBoxW(hwnd, L"保存失败：无法写入文件。", L"保存拼图", MB_ICONWARNING);
            }
          }
          return 0;
        }
        case IDC_SOLVE: runSolve(); return 0;
        case IDC_INPUT_RESET: resetAllInputs(); return 0;
        case IDC_RESET: rebuildToStep(0); updateButtons(); return 0;
        case IDC_PREV: stepBackward(); return 0;
        case IDC_NEXT: stepForward(); return 0;
        case IDC_AUTO_DIS: g_autoDirection = 1; SetTimer(hwnd, IDC_TIMER, 650, nullptr); return 0;
        case IDC_AUTO_ASM: g_autoDirection = -1; SetTimer(hwnd, IDC_TIMER, 650, nullptr); return 0;
      }
      if (LOWORD(wp) >= IDC_PIECE_BASE && LOWORD(wp) < IDC_PIECE_BASE + 6) {
        selectEditPiece((int)LOWORD(wp) - IDC_PIECE_BASE);
        return 0;
      }
      if (LOWORD(wp) >= IDC_VIEWMODE_BASE && LOWORD(wp) < IDC_VIEWMODE_BASE + 6) {
        int idx = (int)LOWORD(wp) - IDC_VIEWMODE_BASE;
        setPieceDisplayMode(idx, g_pieceDisplayMode[idx] + 1);
        return 0;
      }
      if (LOWORD(wp) == IDC_LENGTH && HIWORD(wp) == CBN_SELCHANGE) {
        g_lengthValue = selectedLength();
        invalidateView();
        return 0;
      }
      if (LOWORD(wp) >= IDC_ACT_BASE && LOWORD(wp) < IDC_ACT_BASE + 12) {
        int pos = (int)LOWORD(wp) - IDC_ACT_BASE;
        toggleEditCell(ACT_ORDER[pos]);
        return 0;
      }
      if (LOWORD(wp) >= IDC_RSTYLE_BASE && LOWORD(wp) < IDC_RSTYLE_BASE + 3) {
        int s = (int)LOWORD(wp) - IDC_RSTYLE_BASE;
        setRenderStyle(s);
        return 0;
      }
      if (LOWORD(wp) >= IDC_SHOW_BASE && LOWORD(wp) < IDC_SHOW_BASE + 6) {
        int idx = (int)LOWORD(wp) - IDC_SHOW_BASE;
        bool visible = SendMessage(g_showChecks[idx], BM_GETCHECK, 0, 0) == BST_CHECKED;
        setPieceDisplayMode(idx, visible ? 0 : 2);
        return 0;
      }
      if (LOWORD(wp) >= IDC_CODE_BASE && LOWORD(wp) < IDC_CODE_BASE + 6 && HIWORD(wp) == EN_CHANGE) {
        int idx = (int)LOWORD(wp) - IDC_CODE_BASE;
        if (idx == g_activePiece) updateCellButtonsFromCode();
        return 0;
      }
      if ((HWND)lp == g_steps && HIWORD(wp) == EN_CHANGE) {
        // 不处理（避免循环）
      }
      return 0;
    case WM_SOLVE_DONE: {
      KillTimer(hwnd, IDC_SOLVE_TIMER);
      g_solving = false;
      g_solveStartTick = 0;
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      LubanSolveSummary *result = reinterpret_cast<LubanSolveSummary *>(lp);
      if (result) {
        showResult(*result);
        delete result;
      }
      updateButtons();
      return 0;
    }
    case WM_TIMER:
      if (wp == IDC_SOLVE_TIMER) {
        if (g_solving) updateSolveTimerText();
        return 0;
      }
      if (wp == IDC_ANIM_TIMER) {
        updatePlaybackAnimation();
        return 0;
      }
      if (wp == IDC_TIMER) {
        if (g_animating) return 0;
        if (g_autoDirection > 0) {
          if (g_currentStep >= (int)g_moves.size()) KillTimer(hwnd, IDC_TIMER);
          else stepForward();
        } else {
          if (g_currentStep <= 0) KillTimer(hwnd, IDC_TIMER);
          else stepBackward();
        }
      }
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, IDC_SOLVE_TIMER);
      KillTimer(hwnd, IDC_ANIM_TIMER);
      KillTimer(hwnd, IDC_TIMER);
      if (g_uiFont) {
        DeleteObject(g_uiFont);
        g_uiFont = nullptr;
      }
      if (g_bigFont) {
        DeleteObject(g_bigFont);
        g_bigFont = nullptr;
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
  g_inst = hInstance;
  g_windowBrush = CreateSolidBrush(RGB(224, 229, 236));
  g_panelBrush = CreateSolidBrush(RGB(242, 245, 249));
  g_editBrush = CreateSolidBrush(RGB(255, 255, 255));
  g_viewFrameBrush = CreateSolidBrush(RGB(14, 20, 33));
  g_uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  g_bigFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  INITCOMMONCONTROLSEX icc{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  WNDCLASSW viewClass = {};
  viewClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  viewClass.lpfnWndProc = ViewProc;
  viewClass.hInstance = hInstance;
  viewClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  viewClass.lpszClassName = L"LubanVoxelView";
  RegisterClassW(&viewClass);

  WNDCLASSW mainClass = {};
  mainClass.lpfnWndProc = MainProc;
  mainClass.hInstance = hInstance;
  mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  mainClass.hbrBackground = g_windowBrush;
  mainClass.lpszClassName = L"LubanWin32Gui";
  RegisterClassW(&mainClass);

  HMENU hMenu = CreateMenu();
  HMENU hFileMenu = CreatePopupMenu();
  AppendMenuW(hFileMenu, MF_STRING, ID_FILE_OPEN, L"打开(&O)...");
  AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVE, L"保存(&S)");
  AppendMenuW(hFileMenu, MF_STRING, ID_FILE_SAVEAS, L"另存为(&A)...");
  AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"文件(&F)");

  HWND hwnd = CreateWindowExW(0, L"LubanWin32Gui", L"鲁班锁六柱轻量版求解器",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                              nullptr, hMenu, hInstance, nullptr);
  ShowWindow(hwnd, nCmdShow);

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return (int)msg.wParam;
}
