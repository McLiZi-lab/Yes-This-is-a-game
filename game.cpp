// =====================================================================
//  恶魔轮盘 3D ASCII 版（Buckshot Roulette 风格视觉重制）
//  游戏逻辑与原版完全一致，仅重写画面层与输入提示层。
//
//  编译（源码必须保存为 UTF-8）：
//    MinGW-w64 : g++ -O2 -std=c++17 -o game.exe game.cpp
//    MSVC      : cl /utf-8 /EHsc /O2 game.cpp
//  运行前建议把控制台字体调小（如 8~10pt 点阵/新宋体），以容纳 120x40。
// =====================================================================
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <cwchar>
#include <conio.h>
using namespace std;

// ------------------------- 屏幕尺寸 -----------------------------------
static const int SW = 120;          // 屏幕宽（字符列）
static const int SH = 40;           // 屏幕高（字符行）
static const int SCENE_H = 29;      // 3D 场景区（0~29 行）
static const int HORIZON = 12;      // 视平线所在行

// ------------------------- 颜色属性 -----------------------------------
static const WORD A_DARK  = 0x08;   // 暗灰
static const WORD A_GRAY  = 0x07;   // 亮灰
static const WORD A_WHITE = 0x0F;   // 白
static const WORD A_RED   = 0x0C;   // 亮红（实弹/血）
static const WORD A_DRED  = 0x04;   // 暗红
static const WORD A_YEL   = 0x0E;   // 黄（道具闪光/奖金）
static const WORD A_CYAN  = 0x0B;   // 青
static const WORD A_GREEN = 0x0A;   // 绿
static const WORD A_MAG   = 0x0D;   // 品红（对手侧）

// ------------------------- 画布（双缓冲） ------------------------------
// 每个单元格存一个宽字符 + 颜色属性。中文等全角字符占 2 个控制台列，
// 因此 put() 会同时占用 (x, y) 与 (x+1, y) 两个格，第二格打上 cont 标记。
struct Cell { wchar_t ch; WORD attr; bool cont; };
static Cell g_canvas[SH][SW];

static void clearBuffer() {
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            g_canvas[y][x].ch = L' ';
            g_canvas[y][x].attr = A_GRAY;
            g_canvas[y][x].cont = false;
        }
}

static bool isWideChar(wchar_t c) {  // 全角/表意文字 → 控制台占 2 列
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xFF00 && c <= 0xFFEF) || c == 0x3000;
}

// 屏幕震动：put() 时统一加偏移，整幅画面一起抖
static int g_shakeX = 0, g_shakeY = 0;

static void put(int x, int y, wchar_t c, WORD attr = A_GRAY) {
    x += g_shakeX; y += g_shakeY;
    if (y < 0 || y >= SH || x < 0 || x >= SW) return;
    if (isWideChar(c)) {                // 全角：占两格
        if (x + 1 >= SW) return;
        g_canvas[y][x].ch = c;      g_canvas[y][x].attr = attr; g_canvas[y][x].cont = false;
        g_canvas[y][x + 1].ch = L' '; g_canvas[y][x + 1].attr = attr; g_canvas[y][x + 1].cont = true;
    } else {
        g_canvas[y][x].ch = c; g_canvas[y][x].attr = attr; g_canvas[y][x].cont = false;
    }
}

static int textWidth(const wstring& s) {  // 计算“渲染列宽”（全角算 2）
    int w = 0;
    for (wchar_t c : s) w += isWideChar(c) ? 2 : 1;
    return w;
}

static void drawText(int x, int y, const wstring& s, WORD attr = A_GRAY) {
    int cx = x;
    for (wchar_t c : s) { put(cx, y, c, attr); cx += isWideChar(c) ? 2 : 1; }
}
static void centerText(int y, const wstring& s, WORD attr = A_GRAY) {
    drawText((SW - textWidth(s)) / 2, y, s, attr);
}
static void rightText(int y, const wstring& s, WORD attr = A_GRAY) {
    drawText(SW - textWidth(s), y, s, attr);
}
static void drawSprite(int x, int y, const vector<wstring>& sp, WORD attr = A_GRAY) {
    for (size_t i = 0; i < sp.size(); i++) drawText(x, y + (int)i, sp[i], attr);
}

// ------------------------- 一次性整屏输出 ------------------------------
// 逐行按“同色连续段”输出：跳过全角字符的第二格（该字符本身会在控制台
// 上渲染成 2 列，恰好覆盖被跳过的格），因此每行渲染列宽恒等于 SW，
// 不会残留旧字符、不闪烁。配合隐藏光标，实现无撕裂整帧刷新。
static void present() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD wr = 0;
    for (int y = 0; y < SH; y++) {
        int x = 0, col = 0;
        while (x < SW) {
            if (g_canvas[y][x].cont) { x++; continue; }   // 孤立续格，跳过
            WORD a = g_canvas[y][x].attr;
            wstring s;
            int colStart = col;
            while (x < SW && !g_canvas[y][x].cont && g_canvas[y][x].attr == a) {
                wchar_t c = g_canvas[y][x].ch;
                s += c; x++;
                col += isWideChar(c) ? 2 : 1;
                if (x < SW && g_canvas[y][x].cont) x++;   // 跳过全角第二格
            }
            SetConsoleTextAttribute(h, a);
            COORD pos = { (SHORT)colStart, (SHORT)y };
            SetConsoleCursorPosition(h, pos);
            WriteConsoleW(h, s.data(), (DWORD)s.size(), &wr, NULL);
        }
    }
}

// ------------------------- 透视投影 ------------------------------------
// 相机位于 (0, CAM_Y, 0)，朝 +z 看。世界坐标 → 屏幕坐标：
//   sx = SW/2 + x·(2·FOV)/z     （x 方向乘 2 补偿字符格子 1:2 的纵横比）
//   sy = HORIZON - (y - CAM_Y)·FOV/z
static const float CAM_Y = 1.7f, FOV = 34.0f;
struct Vec3 { float x, y, z; };

static int projX(Vec3 p) { return (int)(SW / 2.0f + (p.x * 2.0f * FOV) / p.z); }
static int projY(Vec3 p) { return (int)(HORIZON - (FOV * (p.y - CAM_Y)) / p.z); }

// 3D 线段：透视投影保持直线性质，先投影两端点再做屏幕 Bresenham
static void drawLine3D(Vec3 a, Vec3 b, wchar_t c, WORD attr = A_GRAY) {
    int x0 = projX(a), y0 = projY(a), x1 = projX(b), y1 = projY(b);
    int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    while (true) {
        put(x0, y0, c, attr);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

// ------------------------- 控制台初始化 --------------------------------
static CONSOLE_CURSOR_INFO g_savedCursor;
static void initConsole() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleOutputCP(65001);                 // UTF-8 代码页
    SetConsoleTitleW(L"恶魔轮盘 · BUCKSHOT ROULETTE 3D");
    GetConsoleCursorInfo(hOut, &g_savedCursor);
    CONSOLE_CURSOR_INFO ci = g_savedCursor;
    ci.bVisible = FALSE;                       // 隐藏光标
    SetConsoleCursorInfo(hOut, &ci);
    // 固定窗口 120x40：先缩小窗口→设缓冲区→再放大窗口，避免滚动条
    SMALL_RECT tmp = { 0, 0, (SHORT)(SW - 1), (SHORT)(SH - 1) };
    SetConsoleWindowInfo(hOut, TRUE, &tmp);
    COORD sz = { (SHORT)SW, (SHORT)SH };
    SetConsoleScreenBufferSize(hOut, sz);
    SetConsoleWindowInfo(hOut, TRUE, &tmp);
    system("cls");
}
static void setCursorVisible(bool v) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO ci = g_savedCursor;
    ci.bVisible = v ? TRUE : FALSE;
    SetConsoleCursorInfo(hOut, &ci);
}

// ------------------------- 输入辅助 ------------------------------------
// 等待一次按键（替代 system("pause")，避免英文提示破坏画面）
static void waitKey() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    FlushConsoleInputBuffer(hIn);
    INPUT_RECORD rec; DWORD n;
    while (true) {
        ReadConsoleInputW(hIn, &rec, 1, &n);
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) break;
    }
}

// ------------------------- 全局动画状态 --------------------------------
static unsigned g_time = 0;          // 动画时钟（ms）
static int g_flashScreen = 0;        // 0 无  1 红闪（中弹） 2 白闪（空枪回声）
static int g_itemFlash = -1;         // 正在闪光提示的道具编号（0~4）
static unsigned g_itemFlashUntil = 0;
static int g_aim = 0;                // 0 枪在桌上  1 指向对手  2 指向自己
static unsigned g_aimUntil = 0;
static bool g_demonAngry = false;    // 恶魔行动时眼睛变红
static int g_lastKnown = 0;          // 仅显示用：放大镜看到的下一发（0未知 1实 2空）
static bool g_gameOver = false;

static wstring g_msg[3];             // 消息区（30~32 行）
static wstring g_menu[5];            // 菜单区（34~38 行）

static void setMsg(const wstring& a, const wstring& b = L"", const wstring& c = L"") {
    g_msg[0] = a; g_msg[1] = b; g_msg[2] = c;
}
static void clearMsg() { g_msg[0] = g_msg[1] = g_msg[2] = L""; }
static void clearMenu() { for (auto& m : g_menu) m = L""; }

static wstring fmtMoney(double v) {
    wchar_t buf[64]; swprintf(buf, 64, L"%.2f", v); return wstring(buf);
}

// ------------------------- 帧循环 --------------------------------------
// animMs：渲染循环，帧间隔约 30ms，推进 g_time 驱动呼吸/眨眼/闪光
static void renderFrame();
static void animMs(int ms) {
    unsigned t0 = GetTickCount();
    while (true) {
        g_time = GetTickCount();
        renderFrame();
        if (g_time - t0 >= (unsigned)ms) break;
        Sleep(30);
    }
    g_shakeX = g_shakeY = 0;
}

// wait()：对应原版 wait() —— 决策文本后逐个冒出"..."（2 秒节奏不变）
static void wait() {
    wstring base = g_msg[0];
    for (int i = 1; i <= 4; i++) {
        g_msg[0] = base + wstring(i, L'.');
        animMs(500);
    }
    g_msg[0] = base;
}

// 整数输入：画面底部画提示，把（隐藏的）光标定位到提示末尾，
// scanf 的回显恰好落在提示行上，不破坏 3D 场景。
static int inputInt(const wstring& prompt) {
    g_menu[4] = prompt;
    while (true) {
        renderFrame();
        setCursorVisible(true);
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD pos = { (SHORT)(6 + textWidth(prompt)), (SHORT)(38) };   // 提示画在 34+4 行
        SetConsoleCursorPosition(hOut, pos);
        int x;
        if (scanf("%d", &x) == 1) { setCursorVisible(false); g_menu[4] = L""; return x; }
        else { int ch; while ((ch = getchar()) != '\n' && ch != EOF); }
        setCursorVisible(false);
    }
}
// =====================================================================
//  Part 2：精灵图与 3D 场景
// =====================================================================

// ------------------------- 游戏状态（与原版完全一致） ------------------
int Your = 6, Other = 6;
wstring daojuname[5] = { L"放大镜", L"手铐", L"小刀", L"烟", L"饮料" };
double Yourmoney;
int shi, kong;
int q[10], qlen;              // 1 实 2 空
int Rand(int x, int y) {
    int A = rand(), B = rand();
    return A * 1ll * B % (y - x + 1) + x;
}
int T;                        // ou->you
int daojulen;
int daoju[10];
int daojulen1;
int daoju1[10];
int Hurt = 1;
int shoukao_you;
int Know;                     // 通过放大镜得知下一发子弹（恶魔 AI 状态）
int shoukaoemo;
int asdasd;

static int gMode = 1;                    // 1 单人 2 双人
static int gMaxHp = 6;
static wstring gNameYou = L"你", gNameOpp = L"恶魔";
static const wchar_t* ITEM_SHORT[5] = { L"镜", L"铐", L"刀", L"烟", L"饮" };

// ------------------------- 恶魔头部精灵（'E'=眼睛占位符） --------------
static const wchar_t* DEMON_ART[] = {
L"   ,/\\,                         ,/\\,",
L"  /    \\                       /    \\",
L" |      \\.                   ./      |",
L"  \\      \\.                 ./      /",
L"   \\       \\.  _________  ./       /",
L"    \\        \\/         \\/        /",
L"     \\                          /",
L"      \\    ,--------------.    /",
L"       \\  /                \\  /",
L"        |                    |",
L"        |     E        E     |",
L"        |                    |",
L"        |   ,----------.     |",
L"        |  (  V   V   V )    |",
L"        |   `----------'     |",
L"         \\                 /",
L"          \\               /",
L"           \\,___________,/",
};
// 双人模式对手：人类头部轮廓
static const wchar_t* HUMAN_ART[] = {
L"        ______________",
L"      /                \\",
L"     /                  \\",
L"    /    E         E     \\",
L"   |                      |",
L"   |          __          |",
L"    \\        (  )        /",
L"     \\        --        /",
L"      \\                /",
L"        \\,__________,/",
};

// ------------------------- 霰弹枪精灵（桌上，指向对面） ----------------
static const wchar_t* GUN_TABLE[] = {
L"       ___",
L"      |   |",
L"      |  |",
L"      |( |",
L"     ==|  |==",
L"    /==|  |==\\",
L"   /___|  |___\\",
L"   \\___|__|___/",
L"   [___][__][___]",
};
// 指向自己：第一人称枪口特写
static const wchar_t* GUN_SELF[] = {
L"        ,--------------.",
L"      /                  \\",
L"     /    ,----------.    \\",
L"    /    /            \\    \\",
L"   |    |     ____     |    |",
L"   |    |    /    \\    |    |",
L"   |    |   |  #### |   |    |",
L"   |    |    \\ ____ /    |    |",
L"    \\    \\              /    /",
L"      \\    '---------'    /",
L"        '--------------'",
};

// ------------------------- 画生命值（♥/♡） ----------------------------
static void drawHearts(int x, int y, int hp, int maxhp) {
    for (int i = 0; i < maxhp; i++)
        put(x + i, y, i < hp ? L'\x2665' : L'\x2661', i < hp ? A_RED : A_DARK);
}
static void drawHpBar(int x, int y, int hp, int maxhp) {   // 备用血条样式
    wstring s = L"[";
    for (int i = 0; i < maxhp; i++) s += (i < hp ? L'\x2588' : L'-');
    s += L"]";
    drawText(x, y, s, A_RED);
}

// ------------------------- 道具栏（桌面上，带数量+闪光） ---------------
static void drawItemRow(int x, int y, int items[10], bool yours) {
    int cx = x;
    for (int i = 0; i <= 4; i++) {
        bool flash = (i == g_itemFlash && g_time < g_itemFlashUntil && (g_time / 120) % 2 == 0);
        WORD a = flash ? A_YEL : (items[i] > 0 ? (yours ? A_CYAN : A_MAG) : A_DARK);
        wstring s = wstring(L"[") + ITEM_SHORT[i] + L"]x" + to_wstring(items[i]);
        drawText(cx, y, s, a);
        cx += 10;
    }
}

// ------------------------- 桌子（透视梯形） ----------------------------
// 世界坐标：桌面 y=1.0，近边 z=2.2，远边 z=6.5，半宽 x=1.7
// 背景/地面：最远层，最先画（之后所有元素都会覆盖它）
static void drawBackground() {
    // 地面：视平线下远处的稀疏点，制造纵深
    for (int y = HORIZON + 1; y < SCENE_H; y++) {
        float z = FOV * CAM_Y / (y - HORIZON);
        if (z < 2.4f) continue;
        for (int x = (y % 2) ? 3 : 9; x < SW; x += 12)
            put(x, y, L'.', A_DARK);
    }
    // 背景微光（顶部的暗点，像地下室吊灯）
    for (int x = 10; x < SW; x += 17) put(x, (x * 7) % (HORIZON - 2), L'.', A_DARK);
}

static void drawTable() {
    Vec3 NL = { -1.7f, 1.0f, 2.2f }, NR = { 1.7f, 1.0f, 2.2f };
    Vec3 FL = { -1.7f, 1.0f, 6.5f }, FR = { 1.7f, 1.0f, 6.5f };
    // 桌面填充：按屏幕行反解 z（z = FOV·(CAM_Y-1)/ (row-HORIZON)），
    // 再算该深度的左右半宽，逐行铺明暗梯度字符 .:-=+*#%@
    for (int y = HORIZON + 1; y <= projY(NL); y++) {
        float z = FOV * (CAM_Y - 1.0f) / (y - HORIZON);
        if (z < 2.2f || z > 6.5f) continue;
        int half = (int)(1.7f * 2.0f * FOV / z);
        int idx = (int)((z - 2.2f) / 4.3f * 8.0f);
        if (idx < 0) idx = 0; if (idx > 7) idx = 7;
        wchar_t c = L".:-=+*#%"[7 - idx];          // 近处更亮
        for (int x = SW / 2 - half; x <= SW / 2 + half; x++)
            put(x, y, c, A_DARK);
    }
    // 桌沿高光
    drawLine3D(NL, NR, L'#', A_GRAY);   // 近沿
    drawLine3D(FL, FR, L'=', A_DARK);   // 远沿
    drawLine3D(NL, FL, L':', A_DARK);   // 左沿
    drawLine3D(NR, FR, L':', A_DARK);   // 右沿
    // 桌子前立面（近沿下方数行，表现厚度）
    int nearY = projY(NL);
    for (int y = nearY + 1; y <= nearY + 3 && y < SCENE_H; y++) {
        int half = (int)(1.7f * 2.0f * FOV / 2.2f);
        for (int x = SW / 2 - half; x <= SW / 2 + half; x++)
            put(x, y, L'=', A_DARK);
        put(SW / 2 - half, y, L'|', A_GRAY);
        put(SW / 2 + half, y, L'|', A_GRAY);
    }
}

// ------------------------- 子弹展示（桌面上） --------------------------
// 只显示剩余数量（顺序保密）：实弹 ●（红），空弹 ○（灰）
static void drawShells() {
    drawText(4, 19, L"弹药", A_GRAY);
    int x = 11;
    for (int i = 0; i < shi; i++) { put(x++, 19, L'\x25CF', A_RED); }        // ●
    if (shi && kong) put(x++, 19, L' ', A_GRAY);
    for (int i = 0; i < kong; i++) { put(x++, 19, L'\x25CB', A_DARK); }      // ○
    if (g_lastKnown) {   // 放大镜情报：下一发
        wstring s = wstring(L"下一发:") + (g_lastKnown == 1 ? L"实弹" : L"空弹");
        drawText(96, 19, s, g_lastKnown == 1 ? A_RED : A_DARK);
    }
}

// ------------------------- 枪 ------------------------------------------
static void drawGun() {
    int w = 17;
    if (g_aim == 2) {                        // 指向自己：枪口对屏
        drawSprite(SW / 2 - 13, 14, vector<wstring>(begin(GUN_SELF), end(GUN_SELF)), A_WHITE);
    } else {
        // 桌上 / 指向对手：都画在桌面中央（指向对手时加亮）
        WORD a = (g_aim == 1 && g_time < g_aimUntil && (g_time / 100) % 2 == 0) ? A_WHITE : A_GRAY;
        drawSprite(SW / 2 - w / 2, 16, vector<wstring>(begin(GUN_TABLE), end(GUN_TABLE)), a);
    }
}

// ------------------------- 对手（恶魔 / 玩家B） ------------------------
// 呼吸：y 随时间 ±1 缓动；眨眼：每 3.4s 闭 150ms；愤怒：眼睛亮红
static void drawOpponent() {
    const wchar_t** art = (gMode == 1) ? (const wchar_t**)DEMON_ART : (const wchar_t**)HUMAN_ART;
    int n = (gMode == 1) ? 18 : 10;
    int breathe = ((g_time / 600) % 2 == 0) ? 0 : 1;
    bool blink = (g_time % 3400) < 150;
    wchar_t eye = blink ? L'-' : (g_demonAngry && gMode == 1 ? L'@' : L'0');
    WORD eyeAttr = blink ? A_DARK : (g_demonAngry && gMode == 1 ? A_RED : (gMode == 1 ? A_YEL : A_CYAN));
    WORD body = (gMode == 1) ? A_DRED : A_GRAY;
    int w = 34;
    int x0 = SW / 2 - w / 2, y0 = 1 + breathe;
    for (int r = 0; r < n; r++) {
        wstring line = art[r];
        for (size_t i = 0; i < line.size(); i++) {
            wchar_t c = line[i];
            if (c == L'E') put(x0 + (int)i, y0 + r, eye, eyeAttr);
            else if (c != L' ') put(x0 + (int)i, y0 + r, c, body);
        }
    }
    // 恶魔愤怒时的暗红光晕
    if (g_demonAngry && gMode == 1 && (g_time / 300) % 2 == 0) {
        put(x0 + 3, y0 + 10, L'*', A_RED); put(x0 + 26, y0 + 10, L'*', A_RED);
    }
}

// ------------------------- HUD ----------------------------------------
static void drawHUD() {
    // 左上：你 / 玩家A
    drawText(2, 0, wstring(L"[ ") + gNameYou + L" ]", A_CYAN);
    drawHearts(2, 1, Your, gMaxHp);
    drawText(2, 2, to_wstring(Your) + L"/" + to_wstring(gMaxHp), A_DARK);
    // 右上：对手
    rightText(0, wstring(L"[ ") + gNameOpp + L" ]", A_MAG);
    drawHearts(SW - 2 - gMaxHp, 1, Other, gMaxHp);
    rightText(2, to_wstring(Other) + L"/" + to_wstring(gMaxHp), A_DARK);
    // 奖金
    centerText(2, wstring(L"奖金 $") + fmtMoney(Yourmoney), A_YEL);
    // 双人模式左右分屏提示线
    if (gMode == 2) drawText(2, 3, L"--- A 侧 ---", A_DARK), rightText(3, L"--- B 侧 ---", A_DARK);
}

// ------------------------- 主场景合成 ----------------------------------
static void drawScene() {
    // 绘制顺序 = 深度排序：背景 → 恶魔（桌子遮住其下半身）→ 桌子 → 枪/弹药/道具
    drawBackground();
    drawOpponent();
    drawTable();
    drawGun();
    drawShells();
    // 道具摆在桌沿上：你的在近沿（row 23），对手的在远沿（row 16）
    drawItemRow(2, 23, daoju, true);
    drawItemRow(2, 16, daoju1, false);
}

// ------------------------- 消息区 + 菜单区 -----------------------------
static void drawOverlay() {
    for (int x = 0; x < SW; x++) put(x, 33, L'-', A_DARK);
    for (int i = 0; i < 3; i++)
        if (!g_msg[i].empty()) centerText(30 + i, g_msg[i], i == 0 ? A_WHITE : A_GRAY);
    for (int i = 0; i < 5; i++)
        if (!g_menu[i].empty()) drawText(6, 34 + i, g_menu[i], i == 0 ? A_YEL : A_GRAY);
    // 全屏闪光：中弹红闪 / 空枪白闪（覆盖非空格单元格颜色）
    if (g_flashScreen) {
        WORD fa = (g_flashScreen == 1) ? A_RED : A_WHITE;
        for (int y = 0; y < SH; y++)
            for (int x = 0; x < SW; x++)
                if (g_canvas[y][x].ch != L' ') g_canvas[y][x].attr = fa;
    }
}

// ------------------------- 整帧渲染 ------------------------------------
static void renderFrame() {
    clearBuffer();
    drawScene();
    drawHUD();
    drawOverlay();
    present();
}
// =====================================================================
//  Part 3：动画与场面事件（装填、开枪、道具、胜负）
// =====================================================================

// ------------------------- 枪口闪光 ------------------------------------
// '*' 从枪口向四周扩散，r 随帧增大；同时画面随机 ±1 抖动（屏幕震动）
static void drawMuzzleFlash(int cx, int cy, int r) {
    for (int i = -r; i <= r; i++) {
        put(cx + i, cy - r, L'*', A_YEL);
        put(cx + i, cy + r, L'*', A_YEL);
        put(cx - r, cy + i, L'*', A_YEL);
        put(cx + r, cy + i, L'*', A_YEL);
    }
    put(cx, cy, L'@', A_WHITE);
}

// ------------------------- 开枪动画 ------------------------------------
// target: 0=对手  1=自己     real: 是否实弹
// 阶段 1 举枪瞄准 → 阶段 2 枪口闪光+震动 → 阶段 3 子弹飞行 → 阶段 4 命中反馈
static void playShootAnim(int target, bool real) {
    // --- 举枪 ---
    g_aim = (target == 0) ? 1 : 2;
    g_aimUntil = g_time + 1200;
    animMs(500);
    // --- 闪光 + 震动 ---
    int mx = SW / 2, my = (target == 0) ? 15 : 20;
    for (int f = 0; f < 4; f++) {
        g_shakeX = Rand(-2, 2); g_shakeY = Rand(-1, 1);
        animMs(0);
        drawMuzzleFlash(mx, my, 1 + f);
        present();
        Sleep(45);
        g_time = GetTickCount();
    }
    g_shakeX = g_shakeY = 0;
    // --- 子弹飞行（射向对手：从枪口飞向恶魔头部） ---
    if (target == 0) {
        int x0 = mx, y0 = my - 2, x1 = SW / 2, y1 = 8;
        for (int step = 0; step <= 6; step++) {
            renderFrame();
            int bx = x0 + (x1 - x0) * step / 6;
            int by = y0 + (y1 - y0) * step / 6;
            put(bx, by, L'\x2022', A_WHITE);          // •
            present();
            Sleep(35);
            g_time = GetTickCount();
        }
    }
    // --- 命中反馈 ---
    if (real) {                     // 实弹：红闪 + 大震动
        g_flashScreen = 1;
        g_demonAngry = (gMode == 1 && target == 1);
        animMs(350);
        g_flashScreen = 0;
        g_shakeX = 2; g_shakeY = 1;
        animMs(150);
    } else {                        // 空弹：白闪一瞬 + 轻震
        g_flashScreen = 2;
        animMs(120);
        g_flashScreen = 0;
        g_shakeX = 1; g_shakeY = 0;
        animMs(180);
    }
    g_shakeX = g_shakeY = 0;
    g_demonAngry = false;
    g_aim = 0;
    animMs(200);
}

// ------------------------- 道具使用动画 --------------------------------
// 对应桌面道具图标高亮闪烁约 0.8s
static void playItemAnim(int itemId) {
    g_itemFlash = itemId;
    g_itemFlashUntil = g_time + 800;
    animMs(800);
    g_itemFlash = -1;
}

// ------------------------- 胜负判定（原逻辑不变） ----------------------
static void drawGameOver(const wstring& title, WORD attr, const wstring& sub) {
    g_gameOver = true;
    clearMsg(); clearMenu();
    for (int t = 0; t < 3; t++) {           // 结算画面淡入
        renderFrame();
        centerText(12, title, attr);
        if (!sub.empty()) centerText(14, sub, A_YEL);
        centerText(18, L"- 按任意键离开这个房间 -", A_DARK);
        present();
        Sleep(400);
    }
    waitKey();
}
void IsOver() {
    if (Your <= 0) {
        drawGameOver(L"你输了", A_RED, L"");
        exit(0);
    }
    if (Other <= 0) {
        drawGameOver(L"你赢了", A_GREEN, wstring(L"你获得了奖金 $") + fmtMoney(Yourmoney));
        exit(0);
    }
}
void IsOver_duo() {
    if (Your <= 0) {
        drawGameOver(L"玩家B赢了", A_GREEN, wstring(L"玩家B获得了奖金 $") + fmtMoney(Yourmoney));
        exit(0);
    } else if (Other <= 0) {
        drawGameOver(L"玩家A赢了", A_GREEN, wstring(L"玩家A获得了奖金 $") + fmtMoney(Yourmoney));
        exit(0);
    }
}

// ------------------------- 装填弹药（原逻辑不变） ----------------------
// 单人：每次补 min(4, 8-已有) 个道具
void build_gun() {
    kong = Rand(1, 4);
    shi = Rand(1, 4);
    qlen = 0;
    // 随机洗出弹序：1 实 2 空（与原版同算法）
    int a1 = kong, a2 = shi;
    for (int i = 1; i <= kong + shi; i++) {
        int sum = Rand(1, a1 + a2);
        if (sum <= a1) { a1--; q[++qlen] = 2; }
        else          { a2--; q[++qlen] = 1; }
    }
    g_lastKnown = 0;                    // 仅显示：新一轮重置情报
    // 你的补给
    wstring myItems;
    int myGot = min(4, 8 - daojulen);   // 先记数量再自增（与原版显示一致）
    daojulen += myGot;
    for (int i = 1; i <= myGot; i++) {
        int kkk = Rand(0, 4);
        daoju[kkk]++;
        myItems += daojuname[kkk];
        if (i != myGot) myItems += L",";
    }
    // 恶魔的补给
    wstring opItems;
    int maxn = min(4, 8 - daojulen1);
    daojulen1 += maxn;
    for (int i = 1; i <= maxn; i++) {
        int kkk = Rand(0, 4);
        daoju1[kkk]++;
        opItems += daojuname[kkk];
        if (i != maxn) opItems += L",";
    }
    // 装填演出：一颗颗子弹飞上桌面
    clearMsg(); clearMenu();
    setMsg(L"恶魔从弹盒中取出弹药……");
    for (int i = 1; i <= shi + kong && i <= 8; i++) {
        renderFrame();
        put(20 + i * 3, 19, i <= shi ? L'\x25CF' : L'\x25CB', i <= shi ? A_RED : A_DARK);
        present(); Sleep(120); g_time = GetTickCount();
    }
    setMsg(wstring(L"装填完毕：") + to_wstring(shi) + L"发实弹, " + to_wstring(kong) + L"发空弹");
    animMs(600);
    setMsg(L"", L"");
    g_msg[0] = wstring(L"[") + gNameYou + L"] 获得了" + to_wstring(myGot) + L"个道具: " + myItems;
    g_msg[1] = wstring(L"[") + gNameOpp + L"] 获得了" + to_wstring(maxn) + L"个道具: " + opItems;
    g_menu[0] = L"- 按任意键开始 -";
    renderFrame();
    waitKey();
    clearMsg(); clearMenu();
}

// 双人：每次补 min(2, 8-已有) 个道具
void build_gun_duo() {
    kong = Rand(1, 4);
    shi = Rand(1, 4);
    qlen = 0;
    int a1 = kong, a2 = shi;
    for (int i = 1; i <= kong + shi; i++) {
        int sum = Rand(1, a1 + a2);
        if (sum <= a1) { a1--; q[++qlen] = 2; }
        else          { a2--; q[++qlen] = 1; }
    }
    g_lastKnown = 0;
    wstring myItems;
    int myGot = min(2, 8 - daojulen);
    daojulen += myGot;
    for (int i = 1; i <= myGot; i++) {
        int kkk = Rand(0, 4);
        daoju[kkk]++;
        myItems += daojuname[kkk];
        if (i != myGot) myItems += L",";
    }
    wstring opItems;
    int maxn = min(2, 8 - daojulen1);
    daojulen1 += maxn;
    for (int i = 1; i <= maxn; i++) {
        int kkk = Rand(0, 4);
        daoju1[kkk]++;
        opItems += daojuname[kkk];
        if (i != maxn) opItems += L",";
    }
    clearMsg(); clearMenu();
    setMsg(L"玩家们清点着弹盒……");
    for (int i = 1; i <= shi + kong && i <= 8; i++) {
        renderFrame();
        put(20 + i * 3, 19, i <= shi ? L'\x25CF' : L'\x25CB', i <= shi ? A_RED : A_DARK);
        present(); Sleep(120); g_time = GetTickCount();
    }
    setMsg(wstring(L"装填完毕：") + to_wstring(shi) + L"发实弹, " + to_wstring(kong) + L"发空弹");
    animMs(600);
    setMsg(L"", L"");
    g_msg[0] = L"[玩家A] 获得了" + to_wstring(myGot) + L"个道具: " + myItems;
    g_msg[1] = L"[玩家B] 获得了" + to_wstring(maxn) + L"个道具: " + opItems;
    g_menu[0] = L"- 按任意键开始 -";
    renderFrame();
    waitKey();
    clearMsg(); clearMenu();
}
// =====================================================================
//  Part 4：单人模式回合逻辑（状态流转与原版逐行对应）
// =====================================================================

// ------------------------- 你的回合 ------------------------------------
void Timeyou() {
    int x;
    while (1) {
        // 场景 HUD 即原版的 stats 输出；菜单画在底部
        clearMsg();
        g_menu[0] = L"现在是你的回合";
        g_menu[1] = L"1.向恶魔开枪   2.向自己开枪";
        g_menu[2] = L"3.使用放大镜   4.使用手铐   5.使用小刀";
        g_menu[3] = L"6.使用烟       7.使用饮料";
        x = inputInt(L"你要做什么？输入数字并回车: ");
        if (1 <= x && x <= 7) break;
        setMsg(L"输入不合法");
        animMs(1145);
        clearMsg();
    }
    clearMenu();

    if (x == 1) {
        setMsg(L"你决定向恶魔开枪");
        T++;
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(shi)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, false);
            setMsg(L"是空弹");
        } else {
            Yourmoney += (double)(5000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            shi--; qlen--; Other -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver();
            if (shoukao_you == 1) {
                shoukao_you = 0;
                Yourmoney += 1000.0;
                g_msg[1] = L"因为你使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 2) {
        setMsg(L"你决定向自己开枪");
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, false);
            setMsg(L"是空弹");
        } else {
            Yourmoney += 5000.0 * (1 + (double)(shi)*1.0 / (double)(shi + kong));
            T++; shi--; qlen--; Your -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver();
            if (shoukao_you == 1) {
                shoukao_you = 0;
                g_msg[1] = L"因为你使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 3) {          // 放大镜
        if (daoju[0]) {
            daoju[0]--; daojulen--;
            playItemAnim(0);
            setMsg(L"你使用了放大镜");
            wait();
            if (q[qlen] == 1) {
                Yourmoney += 2500.0;
                g_lastKnown = 1;
                g_msg[1] = L"你看到了 实弹";
            } else {
                g_lastKnown = 2;
                g_msg[1] = L"你看到了 空弹";
            }
            animMs(500);
        } else {
            setMsg(L"你现在没有放大镜");
            animMs(1145);
        }
    } else if (x == 4) {          // 手铐
        if (daoju[1]) {
            if (!shoukao_you) {
                daoju[1]--; daojulen--;
                playItemAnim(1);
                setMsg(L"你使用了手铐", L"你获得了连开两枪的机会");
                animMs(1145);
            } else {
                setMsg(L"你已经用过手铐了");
                animMs(1145);
            }
        } else {
            setMsg(L"你现在没有手铐");
            animMs(1145);
        }
    } else if (x == 5) {          // 小刀
        if (daoju[2]) {
            if (Hurt == 1) {
                daoju[2]--; daojulen--;
                playItemAnim(2);
                Yourmoney += 500.0;
                Hurt = 2;
                setMsg(L"你使用了小刀", L"若下一发为实弹则伤害+1");
                animMs(1145);
            } else {
                setMsg(L"你已经用过小刀了");
                animMs(1145);
            }
        } else {
            setMsg(L"你现在没有小刀");
            animMs(1145);
        }
    } else if (x == 6) {          // 烟
        if (daoju[3]) {
            if (Your ^ 6) {
                daoju[3]--; daojulen--;
                playItemAnim(3);
                Yourmoney += 500.0;
                Your++;
                setMsg(L"你使用了烟", L"你回复了一点生命");
                animMs(1145);
            } else {
                setMsg(L"你现在不需要烟");
                animMs(1145);
            }
        } else {
            setMsg(L"你现在没有烟");
            animMs(1145);
        }
    } else {                      // 饮料
        if (daoju[4]) {
            daoju[4]--; daojulen--;
            playItemAnim(4);
            setMsg(L"你使用了饮料");
            wait();
            if (q[qlen] == 2) {
                kong--;
                g_lastKnown = 0;
                g_msg[1] = L"你退了一发空弹";
            } else {
                Yourmoney += 500.0;
                shi--;
                g_lastKnown = 0;
                g_msg[1] = L"你退了一发实弹";
            }
            qlen--;
            animMs(500);
        } else {
            setMsg(L"你现在没有饮料");
            animMs(1145);
        }
    }
    animMs(1000);
    clearMsg();
}

// ------------------------- 恶魔开枪（对你） ----------------------------
void fightyou() {
    setMsg(L"恶魔决定向你开枪");
    T++;
    g_demonAngry = true;              // 恶魔行动时眼睛变红
    wait();
    if (q[qlen] == 2) {
        Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
        kong--; qlen--; Hurt = 1;
        Know = 0;
        g_lastKnown = 0;
        playShootAnim(1, false);
        setMsg(L"是空弹");
        if (shoukaoemo) {
            g_msg[1] = L"因为恶魔使用了手铐，所以可以再来一次";
            animMs(500);
            T--;
            shoukaoemo = 0;
        }
    } else {
        Yourmoney += (double)(5000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
        shi--; qlen--; Your -= Hurt; Hurt = 1;
        Know = 0;
        g_lastKnown = 0;
        playShootAnim(1, true);
        setMsg(L"是实弹");
        animMs(500);
        IsOver();
        if (shoukaoemo) {
            g_msg[1] = L"因为恶魔使用了手铐，所以可以再来一次";
            Yourmoney += 1000.0;
            T--;
            animMs(500);
            shoukaoemo = 0;
        }
    }
    g_demonAngry = false;
}

// ------------------------- 恶魔开枪（对自己） --------------------------
void fightemo() {
    setMsg(L"恶魔决定向自己开枪");
    g_demonAngry = true;
    wait();
    if (q[qlen] == 2) {
        Yourmoney += 2000.0 * (1 + (double)(shi)*1.0 / (double)(shi + kong));
        kong--; qlen--;
        Know = 0;
        g_lastKnown = 0;
        playShootAnim(0, false);
        setMsg(L"是空弹");
    } else {
        Yourmoney += 5000.0 * (1 + (double)(kong)*1.0 / (double)(shi + kong));
        shi--; T++; qlen--; Other -= Hurt; Hurt = 1;
        Know = 0;
        g_lastKnown = 0;
        playShootAnim(0, true);
        setMsg(L"是实弹");
        animMs(500);
        IsOver();
        if (shoukaoemo) {
            g_msg[1] = L"因为恶魔使用了手铐，所以可以再来一次";
            T--;
            animMs(500);
            shoukaoemo = 0;
        }
    }
    g_demonAngry = false;
}

// ------------------------- 恶魔的回合（AI 原逻辑不变） ------------------
void Timeother() {
    clearMsg();
    g_demonAngry = true;
    g_menu[0] = L"现在是恶魔的回合……";
    animMs(1500);
    clearMenu();

    if (Other != 6) {
        if (daoju1[3]) {
            daoju1[3]--; daojulen1--;
            playItemAnim(3);
            Other++;
            Yourmoney += 500.0;
            setMsg(L"恶魔使用了烟", L"恶魔回复了一点生命");
            animMs(1145);
            g_demonAngry = false;
            clearMsg();
            return;
        }
    }
    if (Know == 0 && kong == 0) {
        Know = 1;
    }
    if (Know == 0) {
        if (abs(shi - kong) < 2 && kong != 0) {
            if (daoju1[0]) {
                daoju1[0]--; daojulen1--;
                playItemAnim(0);
                setMsg(L"恶魔使用了放大镜");
                wait();
                if (q[qlen] == 1) {
                    Yourmoney += 2500.0;
                    Know = 1;
                    g_msg[1] = L"恶魔看到了 实弹";
                } else {
                    Know = 2;
                    g_msg[1] = L"恶魔看到了 空弹";
                }
                animMs(1145);
                g_demonAngry = false;
                clearMsg();
                return;
            }
        }
    } else if (Know == 1) {
        if (Hurt == 1 && daoju1[2]) {
            daoju1[2]--; daojulen1--;
            Hurt++;
            Yourmoney += 500.0;
            playItemAnim(2);
            setMsg(L"恶魔使用了小刀", L"若下一发为实弹则伤害+1");
            animMs(1145);
            g_demonAngry = false;
            clearMsg();
            return;
        } else {
            if (shi >= kong + 1 && daoju1[1] && shoukaoemo != 1) {
                daoju1[1]--; daojulen1--;
                shoukaoemo = 1;
                playItemAnim(1);
                setMsg(L"恶魔使用了手铐", L"恶魔获得了连开两枪的机会");
                animMs(1145);
                g_demonAngry = false;
                clearMsg();
                return;
            }
            fightyou();
            g_demonAngry = false;
            clearMsg();
            return;
        }
    } else {
        if (daoju1[4]) {
            daoju1[4]--; daojulen1--;
            playItemAnim(4);
            setMsg(L"恶魔使用了饮料");
            wait();
            if (q[qlen] == 2) {
                kong--;
                g_msg[1] = L"恶魔退了一发空弹";
            } else {
                shi--;
                g_msg[1] = L"恶魔退了一发实弹";
            }
            Know = 0;
            qlen--;
            animMs(500);
            animMs(1145);
            g_demonAngry = false;
            clearMsg();
            return;
        } else {
            fightemo();
            animMs(1145);
            g_demonAngry = false;
            clearMsg();
            return;
        }
    }
    if (shi >= kong) {
        fightyou();
    } else {
        fightemo();
    }
    animMs(1145);
    g_demonAngry = false;
    clearMsg();
}

// ------------------------- 单人主循环（原逻辑不变） ---------------------
void Play() {
    while (1) {
        if (shi == 0) {
            build_gun();
            T = 0;
            continue;
        }
        if (T % 2 == 0) {
            Timeyou();
        } else {
            Timeother();
        }
    }
}
// =====================================================================
//  Part 5：双人模式、开场演出、main
// =====================================================================

// ------------------------- 玩家A的回合 ---------------------------------
void Timeyou_duo() {
    int x;
    while (1) {
        clearMsg();
        g_menu[0] = L"现在是玩家A的回合";
        g_menu[1] = L"1.向玩家B开枪   2.向自己开枪";
        g_menu[2] = L"3.使用放大镜   4.使用手铐   5.使用小刀";
        g_menu[3] = L"6.使用烟       7.使用饮料";
        x = inputInt(L"玩家A要做什么？输入数字并回车: ");
        if (1 <= x && x <= 7) break;
        setMsg(L"输入不合法");
        animMs(1145);
        clearMsg();
    }
    clearMenu();

    if (x == 1) {
        setMsg(L"玩家A决定向玩家B开枪");
        T++;
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(shi)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, false);
            setMsg(L"是空弹");
            if (shoukao_you == 1) {
                shoukao_you = 0;
                g_msg[1] = L"因为玩家A使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        } else {
            Yourmoney += (double)(5000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            shi--; qlen--; Other -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver_duo();
            if (shoukao_you == 1) {
                shoukao_you = 0;
                Yourmoney += 1000.0;
                g_msg[1] = L"因为玩家A使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 2) {
        setMsg(L"玩家A决定向自己开枪");
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, false);
            setMsg(L"是空弹");
        } else {
            Yourmoney += 5000.0 * (1 + (double)(shi)*1.0 / (double)(shi + kong));
            T++; shi--; qlen--; Your -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver_duo();
            if (shoukao_you == 1) {
                shoukao_you = 0;
                g_msg[1] = L"因为玩家A使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 3) {          // 放大镜
        if (daoju[0]) {
            daoju[0]--; daojulen--;
            playItemAnim(0);
            setMsg(L"玩家A使用了放大镜");
            wait();
            if (q[qlen] == 1) {
                Yourmoney += 2500.0;
                g_lastKnown = 1;
                g_msg[1] = L"玩家A看到了 实弹";
            } else {
                g_lastKnown = 2;
                g_msg[1] = L"玩家A看到了 空弹";
            }
            animMs(500);
        } else {
            setMsg(L"玩家A现在没有放大镜");
            animMs(1145);
        }
    } else if (x == 4) {          // 手铐
        if (daoju[1]) {
            if (!shoukao_you) {
                daoju[1]--; daojulen--;
                playItemAnim(1);
                setMsg(L"玩家A使用了手铐", L"玩家A获得了连开两枪的机会");
                animMs(1145);
            } else {
                setMsg(L"玩家A已经用过手铐了");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家A现在没有手铐");
            animMs(1145);
        }
    } else if (x == 5) {          // 小刀
        if (daoju[2]) {
            if (Hurt == 1) {
                daoju[2]--; daojulen--;
                playItemAnim(2);
                Yourmoney += 500.0;
                Hurt = 2;
                setMsg(L"玩家A使用了小刀", L"若下一发为实弹则伤害+1");
                animMs(1145);
            } else {
                setMsg(L"玩家A已经用过小刀了");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家A现在没有小刀");
            animMs(1145);
        }
    } else if (x == 6) {          // 烟
        if (daoju[3]) {
            if (Your ^ 4) {
                daoju[3]--; daojulen--;
                playItemAnim(3);
                Yourmoney += 500.0;
                Your++;
                setMsg(L"玩家A使用了烟", L"玩家A回复了一点生命");
                animMs(1145);
            } else {
                setMsg(L"玩家A现在不需要烟");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家A现在没有烟");
            animMs(1145);
        }
    } else {                      // 饮料
        if (daoju[4]) {
            daoju[4]--; daojulen--;
            playItemAnim(4);
            setMsg(L"玩家A使用了饮料");
            wait();
            if (q[qlen] == 2) {
                kong--;
                g_lastKnown = 0;
                g_msg[1] = L"玩家A退了一发空弹";
            } else {
                Yourmoney += 500.0;
                shi--;
                g_lastKnown = 0;
                g_msg[1] = L"玩家A退了一发实弹";
            }
            qlen--;
            animMs(500);
        } else {
            setMsg(L"玩家A现在没有饮料");
            animMs(1145);
        }
    }
    animMs(1000);
    clearMsg();
}

// ------------------------- 玩家B的回合 ---------------------------------
// （含原版的忠实细节：x==2 自射时检查的是 shoukao_you，保持原逻辑不变）
void Timeother_duo() {
    int x;
    while (1) {
        clearMsg();
        g_menu[0] = L"现在是玩家B的回合";
        g_menu[1] = L"1.向玩家A开枪   2.向自己开枪";
        g_menu[2] = L"3.使用放大镜   4.使用手铐   5.使用小刀";
        g_menu[3] = L"6.使用烟       7.使用饮料";
        x = inputInt(L"玩家B要做什么？输入数字并回车: ");
        if (1 <= x && x <= 7) break;
        setMsg(L"输入不合法");
        animMs(1145);
        clearMsg();
    }
    clearMenu();

    if (x == 1) {
        setMsg(L"玩家B决定向玩家A开枪");
        T++;
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(shi)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, false);
            setMsg(L"是空弹");
            if (shoukaoemo == 1) {
                shoukaoemo = 0;
                g_msg[1] = L"因为玩家B使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        } else {
            Yourmoney += (double)(5000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            shi--; qlen--; Your -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(0, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver_duo();
            if (shoukaoemo == 1) {
                shoukaoemo = 0;
                Yourmoney += 1000.0;
                g_msg[1] = L"因为玩家B使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 2) {
        setMsg(L"玩家B决定向自己开枪");
        wait();
        if (q[qlen] == 2) {
            Yourmoney += (double)(2000.0 * (Hurt * 1.0) * (1 + (double)(kong)*1.0 / (double)(shi + kong)));
            kong--; qlen--; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, false);
            setMsg(L"是空弹");
        } else {
            Yourmoney += 5000.0 * (1 + (double)(shi)*1.0 / (double)(shi + kong));
            T++; shi--; qlen--; Other -= Hurt; Hurt = 1;
            g_lastKnown = 0;
            playShootAnim(1, true);
            setMsg(L"是实弹");
            animMs(500);
            IsOver_duo();
            if (shoukao_you == 1) {           // 原版如此，忠实保留
                shoukao_you = 0;
                g_msg[1] = L"因为玩家B使用了手铐，所以可以再来一次";
                animMs(500);
                T--;
            }
        }
    } else if (x == 3) {          // 放大镜
        if (daoju1[0]) {
            daoju1[0]--; daojulen1--;
            playItemAnim(0);
            setMsg(L"玩家B使用了放大镜");
            wait();
            if (q[qlen] == 1) {
                Yourmoney += 2500.0;
                g_lastKnown = 1;
                g_msg[1] = L"玩家B看到了 实弹";
            } else {
                g_lastKnown = 2;
                g_msg[1] = L"玩家B看到了 空弹";
            }
            animMs(500);
        } else {
            setMsg(L"玩家B现在没有放大镜");
            animMs(1145);
        }
    } else if (x == 4) {          // 手铐
        if (daoju1[1]) {
            if (!shoukaoemo) {
                daoju1[1]--; daojulen1--;
                playItemAnim(1);
                setMsg(L"玩家B使用了手铐", L"玩家B获得了连开两枪的机会");
                shoukaoemo = 1;
                animMs(1145);
            } else {
                setMsg(L"玩家B已经用过手铐了");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家B现在没有手铐");
            animMs(1145);
        }
    } else if (x == 5) {          // 小刀
        if (daoju1[2]) {
            if (Hurt == 1) {
                daoju1[2]--; daojulen1--;
                playItemAnim(2);
                Yourmoney += 500.0;
                Hurt = 2;
                setMsg(L"玩家B使用了小刀", L"若下一发为实弹则伤害+1");
                animMs(1145);
            } else {
                setMsg(L"玩家B已经用过小刀了");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家B现在没有小刀");
            animMs(1145);
        }
    } else if (x == 6) {          // 烟
        if (daoju1[3]) {
            if (Other ^ 4) {
                daoju1[3]--; daojulen1--;
                playItemAnim(3);
                Yourmoney += 500.0;
                Other++;
                setMsg(L"玩家B使用了烟", L"玩家B回复了一点生命");
                animMs(1145);
            } else {
                setMsg(L"玩家B现在不需要烟");
                animMs(1145);
            }
        } else {
            setMsg(L"玩家B现在没有烟");
            animMs(1145);
        }
    } else {                      // 饮料
        if (daoju1[4]) {
            daoju1[4]--; daojulen1--;
            playItemAnim(4);
            setMsg(L"玩家B使用了饮料");
            wait();
            if (q[qlen] == 2) {
                kong--;
                g_lastKnown = 0;
                g_msg[1] = L"玩家B退了一发空弹";
            } else {
                Yourmoney += 500.0;
                shi--;
                g_lastKnown = 0;
                g_msg[1] = L"玩家B退了一发实弹";
            }
            qlen--;
            animMs(500);
        } else {
            setMsg(L"玩家B现在没有饮料");
            animMs(1145);
        }
    }
    animMs(1000);
    clearMsg();
}

// ------------------------- 双人主循环（原逻辑不变） ---------------------
void duorenplay() {
    while (1) {
        if (shi == 0) {
            build_gun_duo();
            T = asdasd;
            asdasd++;
            continue;
        }
        if (T % 2 == 0) {
            Timeyou_duo();
        } else {
            Timeother_duo();
        }
    }
}

// ------------------------- 开场画面 ------------------------------------
static const wchar_t* TITLE_ART[] = {
L"  ____  _   _ _____ __  __ _____   ___   ___ _____  _    ____  _____ ____  ",
L" | __ )| | | |_   _|  \\/  | ____| / _ \\ / _ \\_   _|/ \\  |  _ \\| ____|  _ \\ ",
L" |  _ \\| | | | | | | |\\/| |  _|  | | | | | | || | / _ \\ | |_) |  _| | |_) |",
L" | |_) | |_| | | | | |  | | |___ | |_| | |_| || |/ ___ \\|  _ <| |___|  _ < ",
L" |____/ \\___/  |_| |_|  |_|_____| \\___/ \\___/ |_/_/   \\_\\_| \\_\\_____|_| \\_\\",
L"  ____                            ____                 _   _               ",
L" |  _ \\ ___  _ __ _ __ ___  ___   / ___|_ __ ___  __ _| |_(_) ___  _ __    ",
L" | |_) / _ \\| '__| '__/ _ \\/ __| | |  _| '__/ _ \\/ _` | __| |/ _ \\| '_ \\   ",
L" |  _ < (_) | |  | |  (_) \\__ \\ | |_| | | |  __/ (_| | |_| | (_) | | | |   ",
L" |_| \\_\\___/|_|  |_|  \\___/|___/  \\____|_|  \\___|\\__,_|\\__|_|\\___/|_| |_|   ",
};

static void drawTitleScreen(const wstring& subtitle) {
    clearBuffer();
    drawSprite(24, 3, vector<wstring>(begin(TITLE_ART), end(TITLE_ART)), A_DRED);
    centerText(16, L"恶  魔  轮  盘", A_RED);
    if (!subtitle.empty()) centerText(19, subtitle, A_GRAY);
    for (int x = 0; x < SW; x++) put(x, 28, L'-', A_DARK);
    present();
}

// ------------------------- 单人开场（原对话流程不变） -------------------
void danrenplay() {
    drawTitleScreen(L"又来了一位挑战者……");
    animMs(1000);
    clearMsg(); clearMenu();
    int x;
    while (1) {
        drawTitleScreen(L"准备好参与恶魔的游戏吗？胜者带走奖金，败者将会在此长眠");
        centerText(21, L"1.好的      2.没问题", A_WHITE);
        present();
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        setCursorVisible(true);
        SetConsoleCursorPosition(hOut, { 66, 21 });
        if (scanf("%d", &x) == 1 && (x == 1 || x == 2)) break;
        setCursorVisible(false);
    }
    setCursorVisible(false);
    while (1) {
        drawTitleScreen(L"你清楚我们的规则吗？");
        centerText(21, L"1.清楚      2.不清楚", A_WHITE);
        present();
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        setCursorVisible(true);
        SetConsoleCursorPosition(hOut, { 62, 21 });
        if (scanf("%d", &x) == 1 && (x == 1 || x == 2)) break;
        setCursorVisible(false);
    }
    setCursorVisible(false);
    if (x == 1) {
        // 原版：清楚 → 直接跳过规则
    } else {
        // 原版：不清楚 → 展示规则（原文照搬，画在规则画面上）
        clearBuffer();
        centerText(2, L"【 规 则 】", A_YEL);
        const wchar_t* rules[] = {
            L"你和恶魔都各有6点生命",
            L"每一回合开始前，你将知道一共有几发实弹，几发空弹，",
            L"同时双方都将获得4个道具作为补给（上限为8个）",
            L"每一回合，你可以选择对自己开枪，对恶魔开枪或者使用道具",
            L"如果你对自己开枪，若为空弹，则可以继续行动，否则，停止行动",
            L"如果你对恶魔开枪，无论如何，都将停止行动",
            L"",
            L"道具一览：",
            L"放大镜：可以知道下一发子弹是空弹还是实弹",
            L"手铐：增加一次本回合的行动次数",
            L"小刀：若下一发子弹为实弹，则伤害+1",
            L"烟：可以回复1点体力",
            L"饮料：可以退一发子弹",
        };
        for (int i = 0; i < 13; i++) centerText(5 + i, rules[i], A_GRAY);
        centerText(21, L"- 按任意键继续 -", A_DARK);
        present();
        waitKey();
    }
    drawTitleScreen(L"好吧……游戏将要开始了哦");
    animMs(1145);
    animMs(1145);
    clearMsg(); clearMenu();
    Play();
}

// ------------------------- main（原流程不变） ---------------------------
int main() {
    srand(time(0));
    initConsole();
    int x;
    while (1) {
        drawTitleScreen(L"请选择你想要的模式");
        centerText(21, L"1.单人                    2.双人（生命值为4，道具补给为2）", A_WHITE);
        present();
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        setCursorVisible(true);
        SetConsoleCursorPosition(hOut, { 66, 21 });
        if (scanf("%d", &x) == 1 && (x == 1 || x == 2)) break;
        setCursorVisible(false);
    }
    setCursorVisible(false);
    if (x == 1) {
        gMode = 1; gMaxHp = 6;
        gNameYou = L"你";   gNameOpp = L"恶魔";
        danrenplay();
    } else {
        gMode = 2; gMaxHp = 4;
        gNameYou = L"玩家A"; gNameOpp = L"玩家B";
        Your = Other = 4;
        duorenplay();
    }
    return 0;
}
