// PaletteLab & Vision Accessibility - Win32 + GDI (Visual Studio, C++17)
// Luu y: bat /utf-8 trong Project Properties > C/C++ > Command Line > Additional Options
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "gdiplus.lib")
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <ctime>
using std::min; using std::max;
#include <gdiplus.h>

struct Col { int r, g, b; };

enum { ID_BASE = 1, ID_RULE, ID_GEN, ID_RND, ID_IMG, ID_CSS };

// ---------- Du lieu toan cuc ----------
static double LIN[256];
static Col colors[5];
static bool locked[5] = {};
static Col baseCol = { 59, 130, 246 };
static int rule = 0;
static HFONT gFont, gBold, gTitle;
static int iw = 0, ih = 0;
static std::vector<unsigned> simImg[5];

static const wchar_t* NAMES[5] = { L"Thị lực bình thường", L"Protanopia (mù đỏ)", L"Deuteranopia (mù lục)",
                                  L"Tritanopia (mù lam)", L"Achromatopsia (mù màu)" };
static const wchar_t* SHORTN[5] = { L"", L"mù đỏ", L"mù lục", L"mù lam", L"mù màu hoàn toàn" };

// Ma tran Machado 2009 (muc nang nhat). Hang 0 = ma tran don vi.
static const double MAT[5][3][3] = {
    { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } },
    { { .152286, 1.052583, -.204868 },{ .114503, .786281, .099216 },{ -.003882, -.048116, 1.051998 } },
    { { .367322, .860646, -.227968 },{ .280085, .672501, .047413 },{ -.01182, .04294, .968881 } },
    { { 1.255528, -.076749, -.178779 },{ -.078411, .930809, .147602 },{ .004733, .691367, .3039 } },
    { { .2126, .7152, .0722 },{ .2126, .7152, .0722 },{ .2126, .7152, .0722 } } };

// Quy tac hoa sac: {lech Hue, lech Do sang}
static const double RULES[5][5][2] = {
    { { 0, 0 },{ 0, .18 },{ 180, 0 },{ 180, .18 },{ 0, -.25 } },      // Bo tuc
    { { -60, 0 },{ -30, 0 },{ 0, 0 },{ 30, 0 },{ 60, 0 } },            // Tuong dong
    { { 0, 0 },{ 120, 0 },{ 240, 0 },{ 0, .22 },{ 120, .22 } },        // Tam giac
    { { 0, 0 },{ 150, 0 },{ 210, 0 },{ 0, .22 },{ 180, -.2 } },        // Bo tuc ke
    { { 0, -.3 },{ 0, -.15 },{ 0, 0 },{ 0, .15 },{ 0, .3 } } };         // Don sac

// ---------- Chuyen doi mau ----------
static int toS(double c) {
    c = std::min(1.0, std::max(0.0, c));
    return (int)std::lround(255 * (c <= .0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - .055));
}
static Col simulate(Col c, int k) {
    double l[3] = { LIN[c.r], LIN[c.g], LIN[c.b] }, o[3];
    for (int i = 0; i < 3; i++) o[i] = MAT[k][i][0] * l[0] + MAT[k][i][1] * l[1] + MAT[k][i][2] * l[2];
    return { toS(o[0]), toS(o[1]), toS(o[2]) };
}
static void toHSL(Col c, double& h, double& s, double& l) {
    double r = c.r / 255., g = c.g / 255., b = c.b / 255.;
    double M = std::max(std::max(r, g), b), n = std::min(std::min(r, g), b), d = M - n;
    l = (M + n) / 2; h = 0; s = d ? d / (1 - std::fabs(2 * l - 1)) : 0;
    if (d) {
        if (M == r) h = std::fmod((g - b) / d, 6); else if (M == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
        h *= 60; if (h < 0) h += 360;
    }
}
static Col fromHSL(double h, double s, double l) {
    h = std::fmod(h, 360); if (h < 0) h += 360;
    double c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h / 60, 2) - 1)), m = l - c / 2;
    double r = 0, g = 0, b = 0;
    switch ((int)(h / 60)) {
    case 0: r = c; g = x; break; case 1: r = x; g = c; break; case 2: g = c; b = x; break;
    case 3: g = x; b = c; break; case 4: r = x; b = c; break; default: r = c; b = x;
    }
    return { (int)std::lround((r + m) * 255), (int)std::lround((g + m) * 255), (int)std::lround((b + m) * 255) };
}
// WCAG
static double lum(Col c) { return .2126 * LIN[c.r] + .7152 * LIN[c.g] + .0722 * LIN[c.b]; }
static double contrast(Col a, Col b) {
    double x = lum(a), y = lum(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
static const wchar_t* level(double r) { return r >= 7 ? L"AAA" : r >= 4.5 ? L"AA" : r >= 3 ? L"AA (chữ lớn)" : L"Chưa đạt"; }
// Do chenh lech mau CIE76 (Delta E)
static void toLab(Col c, double o[3]) {
    double r = LIN[c.r], g = LIN[c.g], b = LIN[c.b];
    double x = (.4124 * r + .3576 * g + .1805 * b) / .95047, y = .2126 * r + .7152 * g + .0722 * b,
        z = (.0193 * r + .1192 * g + .9505 * b) / 1.08883;
    auto f = [](double t) { return t > .008856 ? std::cbrt(t) : 7.787 * t + 16. / 116; };
    x = f(x); y = f(y); z = f(z);
    o[0] = 116 * y - 16; o[1] = 500 * (x - y); o[2] = 200 * (y - z);
}
static double deltaE(Col a, Col b) {
    double p[3], q[3]; toLab(a, p); toLab(b, q);
    return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}

// ---------- Tao bang mau ----------
static void generate() {
    double h, s, l; toHSL(baseCol, h, s, l);
    for (int i = 0; i < 5; i++) {
        if (locked[i]) continue;
        double L = (l < .15 || l > .9) ? .5 : l;
        L = std::min(.88, std::max(.12, L + RULES[rule][i][1]));
        colors[i] = fromHSL(h + RULES[rule][i][0], std::max(.35, s), L);
    }
}

// ---------- Anh ----------
static void loadImage(const wchar_t* path) {
    Gdiplus::Bitmap bmp(path);
    if (bmp.GetLastStatus() != Gdiplus::Ok) { MessageBoxW(nullptr, L"Không mở được ảnh này.", L"Lỗi", MB_ICONERROR); return; }
    UINT W = bmp.GetWidth(), H = bmp.GetHeight();
    Gdiplus::BitmapData d; Gdiplus::Rect r(0, 0, (INT)W, (INT)H);
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &d) != Gdiplus::Ok) return;
    double s = std::min(1.0, 300.0 / std::max(W, H));
    iw = std::max(1, (int)(W * s)); ih = std::max(1, (int)(H * s));
    for (int k = 0; k < 5; k++) simImg[k].assign((size_t)iw * ih, 0);
    for (int y = 0; y < ih; y++)
        for (int x = 0; x < iw; x++) {
            UINT sx = std::min(W - 1, (UINT)(x / s)), sy = std::min(H - 1, (UINT)(y / s));
            unsigned p = ((unsigned*)((BYTE*)d.Scan0 + (size_t)sy * d.Stride))[sx];
            Col c = { (int)((p >> 16) & 255), (int)((p >> 8) & 255), (int)(p & 255) };
            for (int k = 0; k < 5; k++) { Col o = simulate(c, k); simImg[k][(size_t)y * iw + x] = (o.r << 16) | (o.g << 8) | o.b; }
        }
    bmp.UnlockBits(&d);
}

// ---------- Ve ----------
static void T(HDC dc, int x, int y, const std::wstring& s, COLORREF c, HFONT f) {
    SelectObject(dc, f); SetTextColor(dc, c); TextOutW(dc, x, y, s.c_str(), (int)s.size());
}
static COLORREF cr(Col c) { return RGB(c.r, c.g, c.b); }

static void draw(HDC dc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(RGB(238, 241, 245)); FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const COLORREF INK = RGB(23, 32, 46), MUTE = RGB(91, 102, 120);
    T(dc, 20, 8, L"PaletteLab", INK, gTitle);

    // 5 o mau
    for (int i = 0; i < 5; i++) {
        RECT r = { 20 + i * 194, 95, 20 + i * 194 + 184, 195 };
        HBRUSH b = CreateSolidBrush(cr(colors[i])); FillRect(dc, &r, b); DeleteObject(b);
        double w = contrast(colors[i], { 255, 255, 255 }), k = contrast(colors[i], { 0, 0, 0 });
        bool white = w >= k; double ratio = std::max(w, k);
        COLORREF tc = white ? RGB(255, 255, 255) : RGB(0, 0, 0);
        wchar_t buf[96];
        swprintf_s(buf, L"#%02X%02X%02X", colors[i].r, colors[i].g, colors[i].b);
        T(dc, r.left + 10, r.top + 8, buf, tc, gBold);
        swprintf_s(buf, L"Chữ %s: %.1f:1 (%s)", white ? L"trắng" : L"đen", ratio, level(ratio));
        T(dc, r.left + 10, r.top + 40, buf, tc, gFont);
        T(dc, r.left + 10, r.top + 70, locked[i] ? L"🔒 Đã khóa" : L"Bấm để khóa", tc, gFont);
    }

    // Mo phong
    T(dc, 20, 205, L"Mô phỏng khiếm thị màu", INK, gBold);
    for (int k = 0; k < 5; k++) {
        int y = 235 + k * 36;
        T(dc, 20, y + 6, NAMES[k], INK, gFont);
        for (int i = 0; i < 5; i++) {
            RECT r = { 260 + i * 140, y, 260 + i * 140 + 140, y + 30 };
            HBRUSH b = CreateSolidBrush(cr(simulate(colors[i], k))); FillRect(dc, &r, b); DeleteObject(b);
        }
    }

    // Canh bao
    T(dc, 20, 425, L"Cảnh báo khó phân biệt (ΔE < 12)", INK, gBold);
    std::vector<std::wstring> warn;
    for (int k = 1; k < 5; k++)
        for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) {
            double d = deltaE(simulate(colors[i], k), simulate(colors[j], k));
            if (d < 12) { wchar_t b[200]; swprintf_s(b, L"• Màu %d và màu %d dễ bị nhầm với người %s (ΔE = %.1f)", i + 1, j + 1, SHORTN[k], d); warn.push_back(b); }
        }
    if (warn.empty()) T(dc, 20, 455, L"Mọi cặp màu đều phân biệt được ở cả 4 dạng khiếm thị.", RGB(22, 121, 74), gFont);
    for (size_t i = 0; i < warn.size() && i < 5; i++) T(dc, 20, 455 + (int)i * 20, warn[i], RGB(180, 68, 11), gFont);
    if (warn.size() > 5) T(dc, 20, 455 + 5 * 20, L"… và " + std::to_wstring(warn.size() - 5) + L" cảnh báo khác.", MUTE, gFont);

    // Anh
    T(dc, 20, 585, L"Mô phỏng trên ảnh", INK, gBold);
    if (!iw) { T(dc, 20, 615, L"Chưa có ảnh. Bấm “Mở ảnh” để chọn PNG, JPG hoặc BMP.", MUTE, gFont); return; }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = iw;
    bi.bmiHeader.biHeight = -ih; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    for (int k = 0; k < 5; k++) {
        double s = std::min(185.0 / iw, 110.0 / ih);
        StretchDIBits(dc, 20 + k * 195, 615, (int)(iw * s), (int)(ih * s), 0, 0, iw, ih, simImg[k].data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        T(dc, 20 + k * 195, 615 + 114, NAMES[k], MUTE, gFont);
    }
}

// ---------- Xuat CSS ----------
static void copyCss(HWND hwnd) {
    std::wstring s = L":root {\n";
    for (int i = 0; i < 5; i++) { wchar_t b[64]; swprintf_s(b, L"  --color-%d: #%02x%02x%02x;\n", i + 1, colors[i].r, colors[i].g, colors[i].b); s += b; }
    s += L"}\n";
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h); CloseClipboard();
    MessageBoxW(hwnd, L"Đã sao chép mã CSS vào clipboard.", L"PaletteLab", MB_OK);
}

// ---------- Cua so ----------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        auto btn = [&](const wchar_t* t, int x, int w, int id) {
            CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 52, w, 30, hwnd, (HMENU)(INT_PTR)id, hi, nullptr); };
        btn(L"Chọn màu gốc", 20, 130, ID_BASE);
        HWND cb = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 160, 52, 150, 200, hwnd, (HMENU)ID_RULE, hi, nullptr);
        for (auto n : { L"Bổ túc", L"Tương đồng", L"Tam giác", L"Bổ túc kề", L"Đơn sắc" }) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)n);
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        btn(L"Tạo bảng màu", 320, 120, ID_GEN); btn(L"Màu ngẫu nhiên", 450, 130, ID_RND);
        btn(L"Mở ảnh", 590, 90, ID_IMG); btn(L"Sao chép CSS", 690, 120, ID_CSS);
        EnumChildWindows(hwnd, [](HWND c, LPARAM) -> BOOL { SendMessageW(c, WM_SETFONT, (WPARAM)gFont, TRUE); return TRUE; }, 0);
        generate();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_BASE) {
            static COLORREF cust[16]; CHOOSECOLORW c = { sizeof(c) };
            c.hwndOwner = hwnd; c.rgbResult = cr(baseCol); c.lpCustColors = cust; c.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&c)) { baseCol = { GetRValue(c.rgbResult), GetGValue(c.rgbResult), GetBValue(c.rgbResult) }; generate(); }
        }
        else if (id == ID_RULE && HIWORD(wp) == CBN_SELCHANGE) {
            rule = (int)SendMessageW((HWND)lp, CB_GETCURSEL, 0, 0); generate();
        }
        else if (id == ID_GEN) generate();
        else if (id == ID_RND) { baseCol = fromHSL(rand() % 360, .5 + (rand() % 40) / 100., .4 + (rand() % 20) / 100.); generate(); }
        else if (id == ID_IMG) {
            wchar_t path[MAX_PATH] = {}; OPENFILENAMEW o = { sizeof(o) };
            o.hwndOwner = hwnd; o.lpstrFile = path; o.nMaxFile = MAX_PATH;
            o.lpstrFilter = L"Anh (png, jpg, bmp, gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0"; o.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&o)) loadImage(path);
        }
        else if (id == ID_CSS) { copyCss(hwnd); return 0; }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp), y = HIWORD(lp);
        for (int i = 0; i < 5; i++)
            if (x >= 20 + i * 194 && x < 20 + i * 194 + 184 && y >= 95 && y < 195) { locked[i] = !locked[i]; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps); RECT rc; GetClientRect(hwnd, &rc);
        HDC m = CreateCompatibleDC(hdc); HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(m, bm);
        draw(m, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
        SelectObject(m, old); DeleteObject(bm); DeleteDC(m); EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int nShow) {
    Gdiplus::GdiplusStartupInput gi; ULONG_PTR tok; Gdiplus::GdiplusStartup(&tok, &gi, nullptr);
    for (int i = 0; i < 256; i++) { double c = i / 255.; LIN[i] = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); }
    srand((unsigned)time(nullptr));
    gFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gBold = CreateFontW(20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gTitle = CreateFontW(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"PaletteLab";
    RegisterClassW(&wc);
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = { 0, 0, 1000, 760 }; AdjustWindowRect(&r, st, FALSE);
    HWND h = CreateWindowW(L"PaletteLab", L"PaletteLab & Vision Accessibility", st, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr);
    ShowWindow(h, nShow);
    MSG m; while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    Gdiplus::GdiplusShutdown(tok);
    return 0;
}