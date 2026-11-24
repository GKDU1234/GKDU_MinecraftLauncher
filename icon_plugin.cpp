// ============================================================================
//  Icon customization plugin (icon_plugin.cpp)
//  ----------------------------------------------------------------------------
//  Customizes the launcher window/taskbar icon through the set_app_icon API.
//
//  How to customize:
//    1. Put an "icon.ico" file next to this DLL (inside plugins\), or in the
//       launcher folder one level up.
//    2. If no icon.ico is found, this plugin generates a demo icon so you can
//       see the effect immediately; overwrite icon.ico to use your own.
//
//  Build:
//    D:\Dev-Cpp\MinGW64\bin\g++.exe -O2 -std=c++14 -shared -o icon_plugin.dll icon_plugin.cpp
// ============================================================================

#include <windows.h>
#include <cstring>
#include <string>
#include <vector>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hModule = h;
    return TRUE;
}

static std::string wUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring dirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L"";
    return path.substr(0, p);
}

static bool fileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Generate a demo 32x32 icon as an .ico file (purple background + "GD").
static bool generateDemoIcon(const std::wstring& icoPath) {
    const int size = 32;
    HDC hdc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(hdc);
    if (!mem) { ReleaseDC(NULL, hdc); return false; }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { DeleteDC(mem); ReleaseDC(NULL, hdc); return false; }
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    RECT r = { 0, 0, size, size };
    HBRUSH bg = CreateSolidBrush(RGB(96, 60, 170));   // purple
    FillRect(mem, &r, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(60, 36, 110));
    HPEN oldp = (HPEN)SelectObject(mem, pen);
    Rectangle(mem, 1, 1, size - 1, size - 1);
    SelectObject(mem, oldp);
    DeleteObject(pen);
    SetTextColor(mem, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    HFONT f = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HFONT of = (HFONT)SelectObject(mem, f);
    DrawTextW(mem, L"GD", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, of);
    DeleteObject(f);
    GdiFlush();

    // Read BGRA pixels (top-down).
    int rowBytes = size * 4;
    std::vector<unsigned char> xorMask((size_t)rowBytes * size);
    memcpy(&xorMask[0], bits, xorMask.size());

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, hdc);

    // AND mask: all zero = fully opaque.
    int andRow = (size + 7) / 8;
    std::vector<unsigned char> andMask((size_t)andRow * size, 0);

    // Build the .ico file in memory.
    std::vector<unsigned char> out;
    unsigned char hdr[6] = { 0, 0, 1, 0, 1, 0 };
    out.insert(out.end(), hdr, hdr + 6);
    int imgSize = 40 + (int)xorMask.size() + (int)andMask.size();
    unsigned char entry[16];
    memset(entry, 0, 16);
    entry[0] = (unsigned char)size;            // width
    entry[1] = (unsigned char)size;            // height
    entry[4] = 1;                              // planes (little-endian)
    entry[6] = 32;                             // bitCount
    entry[8] = (unsigned char)(imgSize & 0xFF);
    entry[9] = (unsigned char)((imgSize >> 8) & 0xFF);
    entry[10] = (unsigned char)((imgSize >> 16) & 0xFF);
    entry[11] = (unsigned char)((imgSize >> 24) & 0xFF);
    entry[12] = 22;                            // image offset = 6 + 16
    out.insert(out.end(), entry, entry + 16);

    unsigned char bih[40];
    memset(bih, 0, 40);
    bih[0] = 40;                               // biSize
    bih[4] = (unsigned char)size;              // width
    bih[8] = (unsigned char)(size * 2);        // height (XOR + AND)
    bih[12] = 1;                               // planes
    bih[14] = 32;                              // bitCount
    int xorSize = (int)xorMask.size();
    bih[20] = (unsigned char)(xorSize & 0xFF);
    bih[21] = (unsigned char)((xorSize >> 8) & 0xFF);
    bih[22] = (unsigned char)((xorSize >> 16) & 0xFF);
    bih[23] = (unsigned char)((xorSize >> 24) & 0xFF);
    out.insert(out.end(), bih, bih + 40);
    out.insert(out.end(), xorMask.begin(), xorMask.end());
    out.insert(out.end(), andMask.begin(), andMask.end());

    HANDLE hf = CreateFileW(icoPath.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hf, &out[0], (DWORD)out.size(), &written, NULL);
    CloseHandle(hf);
    return ok && written == out.size();
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->log("INFO", "\u56FE\u6807\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    if (!host->set_app_icon) {
        host->log("WARN", "host does not support set_app_icon");
        return 0;
    }

    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return 0;
    std::wstring dllPath(buf, n);
    std::wstring pluginsDir = dirOf(dllPath);
    std::wstring launcherDir = dirOf(pluginsDir);

    std::wstring cand1 = pluginsDir + L"\\icon.ico";
    std::wstring cand2 = launcherDir + L"\\icon.ico";

    std::wstring chosen;
    if (fileExistsW(cand1)) chosen = cand1;
    else if (fileExistsW(cand2)) chosen = cand2;
    else {
        chosen = cand1;
        host->log("INFO", "\u672A\u627E\u5230 icon.ico\uFF0C\u5DF2\u751F\u6210\u6F14\u793A\u56FE\u6807");
        if (!generateDemoIcon(chosen)) chosen.clear();
    }

    if (chosen.empty()) {
        host->log("WARN", "failed to prepare icon.ico");
        return 0;
    }
    std::string p = wUtf8(chosen);
    if (host->set_app_icon(p.c_str()) == 0) {
        host->log("INFO", "\u5DF2\u5E94\u7528\u81EA\u5B9A\u4E49\u56FE\u6807");
        host->log("INFO", p.c_str());
    } else {
        host->log("WARN", "set_app_icon failed");
    }
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) {
    g_host = 0;
}

PLUGIN_EXPORT const char* plugin_name(void) { return "icon_plugin"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
