// ============================================================================
//  command_lines.dll - interactive command-line console for GDML launcher
//  Registers a "Command line" button on the main window (via add_button);
//  clicking it opens an independent, non-modal console window. Commands typed
//  in the input box are executed and their output is shown in the same window.
//
//  Commands: help / log / get / set / time / echo / history / clear / about
//            launch / mods / mod / plugins / openconfig / cat / open
//            gamedir / versions / whoami / mem
// ============================================================================

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <iterator>
#include <thread>
#include <mutex>
#include <fstream>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;

static HWND g_win = NULL;      // console window
static HWND g_output = NULL;   // read-only output box
static HWND g_input = NULL;    // command input box
static std::mutex g_mtx;       // guards history file + window handles
static WNDPROC g_oldEditProc = NULL;

// Tab-completion command list
static const char* g_commands[] = {
    "help", "log", "get", "set", "time", "echo", "history", "clear", "cls", "about", "ver",
    "launch", "mods", "mod", "plugins", "openconfig", "cat", "open", "gamedir", "versions", "whoami", "mem",
    "seedmap", "seedfind"
};
static std::string g_lastPrefix;
static int g_matchIndex = 0;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hModule = h;
    return TRUE;
}

// ---- encoding helpers ----
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
static std::string dllDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return "";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return "";
    return wUtf8(p.substr(0, pos));
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---- history (append to file with mutex lock) ----
static void appendHistory(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::ofstream f((dllDir() + "\\command_history.log").c_str(), std::ios::app);
    if (f.is_open()) {
        time_t t = time(NULL);
        struct tm* tmv = localtime(&t);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
        f << "[" << ts << "] " << line << "\n";
        f.close();
    }
}

// ---- output helpers ----
static void appendOutput(const std::string& text) {
    HWND out = g_output;
    if (!out) return;
    std::wstring w = u8w(text);
    int len = GetWindowTextLengthW(out);
    SendMessageW(out, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(out, EM_REPLACESEL, (WPARAM)FALSE, (LPARAM)w.c_str());
}
static void clearOutput() {
    if (g_output) SetWindowTextW(g_output, L"");
}

// ---- command helpers ----
static void openPath(const std::string& path) {
    ShellExecuteW(NULL, L"open", u8w(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static std::string readFileText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

// list mods with enable/disable state
static void listMods() {
    const char* md = g_host ? g_host->get_setting("modDir") : NULL;
    if (!md || !*md) { appendOutput("\u65E0\u6CD5\u83B7\u53D6 mod \u76EE\u5F55\r\n"); return; }
    std::wstring dir = u8w(md);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { appendOutput("mod \u76EE\u5F55\u4E3A\u7A7A\u6216\u4E0D\u5B58\u5728\r\n"); return; }
    int cnt = 0;
    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string n = wUtf8(name);
        if (n.find(".jar") == std::string::npos) continue;
        bool disabled = (n.size() > 9 && n.substr(n.size() - 9) == ".disabled");
        appendOutput(std::string(disabled ? "[\u7981\u7528] " : "[\u542F\u7528] ") + n + "\r\n");
        cnt++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (cnt == 0) appendOutput("\u65E0 mod\r\n");
}

// normalize a mod name by stripping .jar / .jar.disabled suffixes
static std::string normalizeModName(std::string n) {
    if (n.size() > 9 && n.substr(n.size() - 9) == ".disabled") n = n.substr(0, n.size() - 9);
    if (n.size() > 4 && n.substr(n.size() - 4) == ".jar") n = n.substr(0, n.size() - 4);
    return n;
}

// enable=true -> xxx.jar.disabled -> xxx.jar ; enable=false -> xxx.jar -> xxx.jar.disabled
static void setModState(const std::string& name, bool enable) {
    const char* md = g_host ? g_host->get_setting("modDir") : NULL;
    if (!md || !*md) { appendOutput("\u65E0\u6CD5\u83B7\u53D6 mod \u76EE\u5F55\r\n"); return; }
    std::wstring dir = u8w(md);
    std::string base = normalizeModName(name);
    std::wstring src = dir + L"\\" + u8w(base + (enable ? ".jar.disabled" : ".jar"));
    std::wstring dst = dir + L"\\" + u8w(base + (enable ? ".jar" : ".jar.disabled"));
    if (MoveFileW(src.c_str(), dst.c_str())) {
        appendOutput("\u5DF2\u5B8C\u6210\r\n");
    } else {
        appendOutput("\u5931\u8D25\uFF08\u8BF7\u68C0\u67E5 mod \u540D\u662F\u5426\u6B63\u786E\uFF09\r\n");
    }
}

// list installed versions under gameDir/versions
static void listVersions() {
    const char* gd = g_host ? g_host->get_setting("gameDir") : NULL;
    if (!gd || !*gd) return;
    std::wstring vdir = u8w(std::string(gd)) + L"\\versions";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((vdir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { appendOutput("\u65E0\u5DF2\u5B89\u88C5\u7248\u672C\r\n"); return; }
    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        appendOutput(wUtf8(name) + "\r\n");
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ---- command execution ----
static const char* HELP_TEXT =
    "\u57FA\u7840\u547D\u4EE4:\r\n"
    "  help                  \u663E\u793A\u672C\u5E2E\u52A9\r\n"
    "  log <\u6D88\u606F>           \u5199\u5165\u542F\u52A8\u5668\u65E5\u5FD7\r\n"
    "  get <key>             \u8BFB\u53D6\u8BBE\u7F6E (gameDir/username/version/modDir/multiAccounts/launcherDir)\r\n"
    "  set <key> <value>     \u5199\u5165\u8BBE\u7F6E\r\n"
    "  time                  \u663E\u793A\u5F53\u524D\u65E5\u671F\u65F6\u95F4\r\n"
    "  echo <\u6587\u672C>         \u56DE\u663E\u6587\u672C\r\n"
    "  history               \u663E\u793A\u547D\u4EE4\u5386\u53F2\r\n"
    "  clear | cls           \u6E05\u7A7A\u8F93\u51FA\r\n"
    "  about | ver           \u5173\u4E8E\u63D2\u4EF6\r\n"
    "\r\n\u6E38\u620F\u4E0E\u4E0B\u8F7D:\r\n"
    "  launch [\u7248\u672C]       \u542F\u52A8\u6E38\u620F(\u7A7A=\u5F53\u524D\u9009\u4E2D)\r\n"
    "  versions              \u5217\u51FA\u5DF2\u5B89\u88C5\u7248\u672C\r\n"
    "  mods                  \u5217\u51FA mod(\u542B\u542F\u7528/\u7981\u7528\u72B6\u6001)\r\n"
    "  mod disable <\u540D>    \u5C4F\u853D mod\r\n"
    "  mod enable <\u540D>     \u542F\u7528 mod\r\n"
    "  mem <MB>              \u8BBE\u7F6E\u5185\u5B58\r\n"
    "\r\n\u63D2\u4EF6\u4E0E\u914D\u7F6E:\r\n"
    "  plugins               \u5217\u51FA\u5DF2\u52A0\u8F7D\u63D2\u4EF6\r\n"
    "  openconfig            \u6253\u5F00\u542F\u52A8\u5668\u914D\u7F6E\u6587\u4EF6\r\n"
    "  cat <\u6587\u4EF6>          \u67E5\u770B\u6587\u4EF6\u5185\u5BB9\r\n"
    "  open <\u8DEF\u5F84>         \u6253\u5F00\u6587\u4EF6/\u6587\u4EF6\u5939\r\n"
    "  gamedir               \u6253\u5F00\u6E38\u620F\u76EE\u5F55\r\n"
    "  whoami                \u663E\u793A\u5F53\u524D\u8D26\u53F7\r\n"
    "\r\n\u79CD\u5B50\u5DE5\u5177:\r\n"
    "  seedmap open|close    \u6253\u5F00/\u5173\u95ED\u79CD\u5B50\u5730\u56FE\u7A97\u53E3\r\n"
    "  seedfind open|close   \u6253\u5F00/\u5173\u95ED\u79CD\u5B50\u67E5\u627E\u7A97\u53E3\r\n";

static void runCommand(const std::string& raw) {
    std::string line = trim(raw);
    appendOutput("> " + line + "\r\n");
    appendHistory(line);
    if (line.empty()) return;

    std::string cmd = line, arg;
    size_t sp = line.find(' ');
    if (sp != std::string::npos) {
        cmd = line.substr(0, sp);
        arg = trim(line.substr(sp + 1));
    }

    if (cmd == "help") {
        appendOutput(HELP_TEXT);
    } else if (cmd == "log") {
        if (arg.empty()) { appendOutput("\u7528\u6CD5: log <\u6D88\u606F>\r\n"); return; }
        if (g_host) g_host->log("INFO", arg.c_str());
        appendOutput("\u5DF2\u5199\u5165\u65E5\u5FD7\r\n");
    } else if (cmd == "get") {
        if (arg.empty()) { appendOutput("\u7528\u6CD5: get <key>\r\n"); return; }
        const char* v = g_host ? g_host->get_setting(arg.c_str()) : NULL;
        appendOutput(std::string(arg) + " = " + (v ? v : "(null)") + "\r\n");
    } else if (cmd == "set") {
        size_t sp2 = arg.find(' ');
        if (sp2 == std::string::npos) { appendOutput("\u7528\u6CD5: set <key> <value>\r\n"); return; }
        std::string k = trim(arg.substr(0, sp2));
        std::string v = trim(arg.substr(sp2 + 1));
        if (g_host) g_host->set_setting(k.c_str(), v.c_str());
        appendOutput("set " + k + " = " + v + "\r\n");
    } else if (cmd == "time") {
        time_t t = time(NULL);
        struct tm* tmv = localtime(&t);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmv);
        appendOutput(std::string(ts) + "\r\n");
    } else if (cmd == "echo") {
        appendOutput(arg + "\r\n");
    } else if (cmd == "history") {
        std::ifstream f((dllDir() + "\\command_history.log").c_str());
        if (f.is_open()) {
            std::string l;
            while (std::getline(f, l)) appendOutput(l + "\r\n");
            f.close();
        } else {
            appendOutput("\u6682\u65E0\u5386\u53F2\u8BB0\u5F55\r\n");
        }
    } else if (cmd == "clear" || cmd == "cls") {
        clearOutput();
    } else if (cmd == "about" || cmd == "ver") {
        appendOutput("command_lines 1.1 - GDML \u547D\u4EE4\u884C\u63D2\u4EF6\r\n");
    } else if (cmd == "launch") {
        if (!g_host || !g_host->launch_game) { appendOutput("\u4E0D\u652F\u6301\u542F\u52A8\r\n"); return; }
        int r = g_host->launch_game(arg.c_str());
        appendOutput(r == 0 ? "\u5DF2\u53D1\u8D77\u542F\u52A8\r\n" : "\u542F\u52A8\u5931\u8D25\r\n");
    } else if (cmd == "mods") {
        listMods();
    } else if (cmd == "mod") {
        size_t sp2 = arg.find(' ');
        if (sp2 == std::string::npos) { appendOutput("\u7528\u6CD5: mod disable/enable <\u540D>\r\n"); return; }
        std::string sub = trim(arg.substr(0, sp2));
        std::string name = trim(arg.substr(sp2 + 1));
        if (sub == "disable") setModState(name, false);
        else if (sub == "enable") setModState(name, true);
        else appendOutput("\u672A\u77E5\u5B50\u547D\u4EE4: " + sub + "\r\n");
    } else if (cmd == "plugins") {
        if (!g_host || !g_host->list_plugins) { appendOutput("\u4E0D\u652F\u6301\r\n"); return; }
        const char* list = g_host->list_plugins();
        if (!list || !*list) { appendOutput("\u65E0\u63D2\u4EF6\r\n"); return; }
        std::string s = list, tok;
        for (size_t i = 0; i <= s.size(); i++) {
            if (i == s.size() || s[i] == ',') { appendOutput("  " + tok + "\r\n"); tok.clear(); }
            else tok += s[i];
        }
    } else if (cmd == "openconfig") {
        if (!g_host || !g_host->get_launcher_dir) { appendOutput("\u4E0D\u652F\u6301\r\n"); return; }
        std::string cfg = std::string(g_host->get_launcher_dir()) + "\\config.json";
        openPath(cfg);
        appendOutput("\u5DF2\u6253\u5F00\u914D\u7F6E\u6587\u4EF6\r\n");
    } else if (cmd == "cat") {
        if (arg.empty()) { appendOutput("\u7528\u6CD5: cat <\u6587\u4EF6>\r\n"); return; }
        std::string txt = readFileText(arg);
        if (txt.empty()) { appendOutput("\u6587\u4EF6\u4E0D\u5B58\u5728\u6216\u4E3A\u7A7A\r\n"); return; }
        appendOutput(txt + "\r\n");
    } else if (cmd == "open") {
        if (arg.empty()) { appendOutput("\u7528\u6CD5: open <\u8DEF\u5F84>\r\n"); return; }
        openPath(arg);
        appendOutput("\u5DF2\u6253\u5F00: " + arg + "\r\n");
    } else if (cmd == "gamedir") {
        const char* gd = g_host ? g_host->get_setting("gameDir") : NULL;
        if (gd && *gd) openPath(gd);
        appendOutput("\u5DF2\u6253\u5F00\u6E38\u620F\u76EE\u5F55\r\n");
    } else if (cmd == "versions") {
        listVersions();
    } else if (cmd == "whoami") {
        const char* u = g_host ? g_host->get_setting("username") : NULL;
        appendOutput(std::string(u && *u ? u : "(null)") + "\r\n");
    } else if (cmd == "mem") {
        if (arg.empty()) { appendOutput("\u7528\u6CD5: mem <MB>\r\n"); return; }
        if (g_host) g_host->set_setting("memory", arg.c_str());
        appendOutput("\u5185\u5B58\u5DF2\u8BBE\u7F6E: " + arg + " MB\r\n");
    } else if (cmd == "seedmap" || cmd == "seedfind") {
        HWND w = FindWindowW(cmd == "seedmap" ? L"GDMLSeedMapWnd" : L"GDMLSeedFindWnd", NULL);
        bool close = (arg == "close");
        if (w) {
            if (close) ShowWindow(w, SW_HIDE);
            else { ShowWindow(w, SW_SHOW); SetForegroundWindow(w); }
        }
        appendOutput(close ? "\u5DF2\u5173\u95ED\r\n" : "\u5DF2\u6253\u5F00\r\n");
    } else {
        appendOutput("\u672A\u77E5\u547D\u4EE4: " + cmd + "\r\n\u8F93\u5165 help \u67E5\u770B\u5E2E\u52A9\r\n");
    }
}

static void submitInput() {
    if (!g_input) return;
    int len = GetWindowTextLengthW(g_input);
    std::wstring w(len, 0);
    GetWindowTextW(g_input, &w[0], len + 1);
    SetWindowTextW(g_input, L"");
    runCommand(wUtf8(w));
}

// Complete the current input against g_commands; cycles on repeated Tab.
static void doTabComplete() {
    if (!g_input) return;
    int len = GetWindowTextLengthW(g_input);
    std::wstring w(len, 0);
    GetWindowTextW(g_input, &w[0], len + 1);
    std::string cur = trim(wUtf8(w));

    std::vector<std::string> matches;
    size_t n = sizeof(g_commands) / sizeof(g_commands[0]);
    for (size_t i = 0; i < n; i++) {
        std::string cmd = g_commands[i];
        if (cmd.size() < cur.size()) continue;
        bool ok = true;
        for (size_t j = 0; j < cur.size(); j++) {
            if (tolower((unsigned char)cmd[j]) != tolower((unsigned char)cur[j])) { ok = false; break; }
        }
        if (ok) matches.push_back(cmd);
    }
    if (matches.empty()) return;

    if (cur != g_lastPrefix) { g_lastPrefix = cur; g_matchIndex = 0; }
    std::string complete = matches[g_matchIndex % matches.size()] + " ";
    SetWindowTextW(g_input, u8w(complete).c_str());
    int sel = (int)u8w(complete).size();
    SendMessageW(g_input, EM_SETSEL, (WPARAM)sel, (LPARAM)sel);
    g_matchIndex++;
}

// subclassed edit proc: Enter runs, Tab completes
static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { submitInput(); return 0; }
    if (m == WM_KEYDOWN && w == VK_TAB) { doTabComplete(); return 0; }
    return CallWindowProcW(g_oldEditProc, h, m, w, l);
}

static LRESULT CALLBACK winProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
        g_output = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
            8, 8, 560, 300, h, (HMENU)1, inst, NULL);
        g_input = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            8, 318, 470, 24, h, (HMENU)2, inst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u6267\u884C", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            486, 316, 82, 28, h, (HMENU)3, inst, NULL);
        g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g_input, GWLP_WNDPROC, (LONG_PTR)editProc);
        appendOutput("GDML \u547D\u4EE4\u884C\u63A7\u5236\u53F0\u5DF2\u542F\u52A8\r\n\u8F93\u5165 help \u67E5\u770B\u53EF\u7528\u547D\u4EE4\r\n\r\n");
        return 0;
    }
    case WM_SIZE: {
        int w2 = LOWORD(l), h2 = HIWORD(l);
        if (g_output) SetWindowPos(g_output, NULL, 0, 0, w2 - 16, h2 - 60, SWP_NOZORDER);
        if (g_input) SetWindowPos(g_input, NULL, 8, h2 - 48, w2 - 100, 24, SWP_NOZORDER);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == 3) { submitInput(); return 0; }
        break;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void windowThread() {
    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(NULL);
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = winProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"GDMLCommandWindow";
    RegisterClassW(&wc);

    g_win = CreateWindowExW(0, L"GDMLCommandWindow", L"GDML \u547D\u4EE4\u884C",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 380, NULL, NULL, inst, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// button callback: show the console window
static int onButton(const PluginEvent* ev) {
    (void)ev;
    if (g_win) {
        ShowWindow(g_win, SW_SHOW);
        SetForegroundWindow(g_win);
        if (g_input) SetFocus(g_input);
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    if (host->add_button) host->add_button("\u547D\u4EE4\u884C", onButton);
    std::thread t(windowThread);
    t.detach();
    host->log("INFO", "\u547D\u4EE4\u884C\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) {
    if (g_win) PostMessageW(g_win, WM_DESTROY, 0, 0);
    g_host = 0;
}

PLUGIN_EXPORT const char* plugin_name(void) { return "command_lines"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.1"; }
