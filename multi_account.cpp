// ============================================================================
//  multi_account.dll - quick offline account switching
//  Maintains a comma-separated account list (multi_accounts) and, before the
//  game launches, shows a picker dialog when more than one account is known.
// ============================================================================

#include <windows.h>
#include <cstdio>
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

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == sep) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += s[i];
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string joinList(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) { if (i) out += ","; out += v[i]; }
    return out;
}

static std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

struct DlgState { std::vector<std::string> accounts; std::string selected; };

static INT_PTR CALLBACK dlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static DlgState* s = NULL;
    switch (m) {
    case WM_INITDIALOG: {
        s = (DlgState*)l;
        HWND list = GetDlgItem(h, 1001);
        for (size_t i = 0; i < s->accounts.size(); i++)
            SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)u8w(s->accounts[i]).c_str());
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK) {
                HWND list = GetDlgItem(h, 1001);
                int idx = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (idx >= 0 && idx < (int)s->accounts.size()) s->selected = s->accounts[idx];
            }
            EndDialog(h, id == IDOK ? 1 : 0);
            return TRUE;
        }
        if (id == 1001 && HIWORD(w) == LBN_DBLCLK) {
            HWND list = GetDlgItem(h, 1001);
            int idx = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx < (int)s->accounts.size()) s->selected = s->accounts[idx];
            EndDialog(h, 1);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static bool showAccountDialog(const std::vector<std::string>& accounts, std::string& selected) {
    std::vector<unsigned char> out;
    auto pushW = [&](uint16_t x) { out.push_back(x & 0xff); out.push_back(x >> 8); };
    auto pushD = [&](uint32_t x) { for (int i = 0; i < 4; i++) out.push_back((x >> (i * 8)) & 0xff); };
    auto pushStr = [&](const wchar_t* s) { size_t n = wcslen(s) + 1; for (size_t i = 0; i < n; i++) { out.push_back(s[i] & 0xff); out.push_back(s[i] >> 8); } };

    pushD(0x80C808C0); pushD(0);
    pushW(3);
    pushW(10); pushW(10); pushW(240); pushW(210);
    pushW(0); pushW(0);
    pushStr(L"\u9009\u62E9\u8D26\u53F7");
    pushW(9); pushStr(L"Microsoft YaHei UI");

    // ListBox
    pushD(0x50810101); pushD(0);
    pushW(10); pushW(10); pushW(220); pushW(160);
    pushW(1001); pushW(0xffff); pushW(0x0083);
    pushW(0); pushW(0);

    // OK button
    pushD(0x50010000); pushD(0);
    pushW(30); pushW(180); pushW(80); pushW(24);
    pushW(IDOK); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u786E\u5B9A"); pushW(0);

    // Cancel button
    pushD(0x50010000); pushD(0);
    pushW(130); pushW(180); pushW(80); pushW(24);
    pushW(IDCANCEL); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u53D6\u6D88"); pushW(0);

    DlgState st;
    st.accounts = accounts;
    INT_PTR r = DialogBoxIndirectParamW(g_hModule, (LPDLGTEMPLATE)out.data(), NULL, dlgProc, (LPARAM)&st);
    if (r == 1 && !st.selected.empty()) { selected = st.selected; return true; }
    return false;
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type != EVENT_GAME_LAUNCHING) return 0;

    const char* cur = g_host->get_setting("username");
    const char* list = g_host->get_setting("multiAccounts");
    std::vector<std::string> accounts = list ? split(list, ',') : std::vector<std::string>();

    // Auto-record the current username if not already in the list
    if (cur && *cur) {
        bool found = false;
        for (size_t i = 0; i < accounts.size(); i++) if (accounts[i] == cur) { found = true; break; }
        if (!found) accounts.push_back(cur);
    }
    g_host->set_setting("multiAccounts", joinList(accounts).c_str());

    if (accounts.size() <= 1) return 0;  // nothing to switch between

    std::string selected;
    if (showAccountDialog(accounts, selected)) {
        g_host->set_setting("username", selected.c_str());
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->log("INFO", "\u591A\u8D26\u53F7\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "multi_account"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
