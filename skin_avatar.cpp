// ============================================================================
//  skin_avatar.dll - set the player skin avatar as the launcher icon
//  Resolves the username -> UUID via Mojang, downloads the avatar PNG from
//  crafatar, converts it to an icon.ico with GDI+, then calls set_app_icon.
// ============================================================================

#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <gdiplus.h>
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

static std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring dllDirW() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return L"";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return p.substr(0, pos);
}

// Minimal WinHTTP GET (https only, follows redirects once). Returns body bytes.
static bool httpGetBytes(const std::wstring& url, std::vector<unsigned char>& out) {
    std::wstring host = url;
    std::wstring path = L"/";
    size_t p = url.find(L"://");
    if (p != std::wstring::npos) host = url.substr(p + 3);
    size_t slash = host.find(L'/');
    if (slash != std::wstring::npos) { path = host.substr(slash); host = host.substr(0, slash); }

    HINTERNET sess = WinHttpOpen(L"GDML-skin-avatar", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!sess) return false;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), 443, 0);
    if (!conn) { WinHttpCloseHandle(sess); return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    bool ok = false;
    if (req) {
        if (WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(req, NULL)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
            if (status >= 200 && status < 300) {
                DWORD avail = 0, read = 0;
                out.clear();
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    std::vector<unsigned char> buf(avail);
                    if (!WinHttpReadData(req, &buf[0], avail, &read)) break;
                    out.insert(out.end(), buf.begin(), buf.begin() + read);
                    if (read == 0) break;
                }
                ok = !out.empty();
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return ok;
}

// Download avatar PNG and convert to a 32x32 icon.ico via GDI+.
static bool makeIconFromPng(const std::wstring& pngPath, const std::wstring& icoPath) {
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput si;
    if (Gdiplus::GdiplusStartup(&token, &si, NULL) != Gdiplus::Ok) return false;

    bool ok = false;
    Gdiplus::Bitmap src(pngPath.c_str());
    if (src.GetLastStatus() == Gdiplus::Ok) {
        const int size = 32;
        Gdiplus::Bitmap thumb(size, size, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&thumb);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(&src, 0, 0, size, size);

        // Read BGRA pixels (top-down)
        std::vector<unsigned char> xorMask((size_t)size * size * 4);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                Gdiplus::Color c;
                thumb.GetPixel(x, y, &c);
                size_t i = ((size_t)y * size + x) * 4;
                xorMask[i + 0] = c.GetBlue();
                xorMask[i + 1] = c.GetGreen();
                xorMask[i + 2] = c.GetRed();
                xorMask[i + 3] = c.GetAlpha();
            }
        }
        std::vector<unsigned char> andMask((size_t)((size + 7) / 8) * size, 0);

        std::vector<unsigned char> out;
        unsigned char hdr[6] = { 0, 0, 1, 0, 1, 0 };
        out.insert(out.end(), hdr, hdr + 6);
        int imgSize = 40 + (int)xorMask.size() + (int)andMask.size();
        unsigned char entry[16];
        memset(entry, 0, 16);
        entry[0] = size; entry[1] = size; entry[4] = 1; entry[6] = 32;
        entry[8] = (unsigned char)(imgSize & 0xFF);
        entry[9] = (unsigned char)((imgSize >> 8) & 0xFF);
        entry[10] = (unsigned char)((imgSize >> 16) & 0xFF);
        entry[11] = (unsigned char)((imgSize >> 24) & 0xFF);
        entry[12] = 22;
        out.insert(out.end(), entry, entry + 16);
        unsigned char bih[40];
        memset(bih, 0, 40);
        bih[0] = 40; bih[4] = size; bih[8] = (unsigned char)(size * 2);
        bih[12] = 1; bih[14] = 32;
        int xs = (int)xorMask.size();
        bih[20] = (unsigned char)(xs & 0xFF); bih[21] = (unsigned char)((xs >> 8) & 0xFF);
        bih[22] = (unsigned char)((xs >> 16) & 0xFF); bih[23] = (unsigned char)((xs >> 24) & 0xFF);
        out.insert(out.end(), bih, bih + 40);
        out.insert(out.end(), xorMask.begin(), xorMask.end());
        out.insert(out.end(), andMask.begin(), andMask.end());

        HANDLE hf = CreateFileW(icoPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ok = WriteFile(hf, &out[0], (DWORD)out.size(), &written, NULL) && written == out.size();
            CloseHandle(hf);
        }
    }
    Gdiplus::GdiplusShutdown(token);
    return ok;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->log("INFO", "\u76AE\u80A4\u5934\u50CF\u63D2\u4EF6\u5DF2\u52A0\u8F7D");

    const char* name = host->get_setting("username");
    if (!name || !*name || !host->set_app_icon) return 0;

    // Resolve username -> UUID
    std::vector<unsigned char> body;
    std::wstring profileUrl = L"https://api.mojang.com/users/profiles/minecraft/" + u8w(name);
    if (!httpGetBytes(profileUrl, body)) return 0;
    std::string json(body.begin(), body.end());
    size_t idp = json.find("\"id\"");
    if (idp == std::string::npos) return 0;
    size_t v1 = json.find('"', idp + 4);
    size_t v2 = json.find('"', v1 + 1);
    if (v1 == std::string::npos || v2 == std::string::npos) return 0;
    std::string uuid = json.substr(v1 + 1, v2 - v1 - 1);
    if (uuid.empty()) return 0;

    // Download avatar PNG
    std::wstring avatarUrl = L"https://crafatar.com/avatars/" + u8w(uuid) + L"?size=64";
    std::vector<unsigned char> png;
    if (!httpGetBytes(avatarUrl, png)) return 0;
    std::wstring pngPath = dllDirW() + L"\\avatar.png";
    HANDLE hf = CreateFileW(pngPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(hf, &png[0], (DWORD)png.size(), &w, NULL);
        CloseHandle(hf);
    }

    std::wstring icoPath = dllDirW() + L"\\avatar.ico";
    if (makeIconFromPng(pngPath, icoPath)) {
        std::string ip = wUtf8(icoPath);
        if (host->set_app_icon(ip.c_str()) == 0) {
            host->log("INFO", "\u5DF2\u5E94\u7528\u76AE\u80A4\u5934\u50CF");
        }
    }
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "skin_avatar"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
