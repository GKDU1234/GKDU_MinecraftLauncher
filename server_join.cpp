// ============================================================================
//  server_join.dll - seed a default server list
//  If servers.dat does not exist, generates a minimal valid NBT file with a
//  preset server (Hypixel) so the multiplayer list is not empty.
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

static std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ---- minimal NBT writer (big-endian, uncompressed) ----
static void nbtString(std::vector<unsigned char>& o, const std::string& s) {
    unsigned short len = (unsigned short)s.size();
    o.push_back((unsigned char)((len >> 8) & 0xff));
    o.push_back((unsigned char)(len & 0xff));
    o.insert(o.end(), s.begin(), s.end());
}
static void nbtInt32(std::vector<unsigned char>& o, int v) {
    for (int i = 3; i >= 0; i--) o.push_back((unsigned char)((v >> (i * 8)) & 0xff));
}
static void tagString(std::vector<unsigned char>& o, const std::string& name, const std::string& value) {
    o.push_back(0x08); nbtString(o, name); nbtString(o, value);
}
static void tagByte(std::vector<unsigned char>& o, const std::string& name, unsigned char value) {
    o.push_back(0x01); nbtString(o, name); o.push_back(value);
}
static void compoundStart(std::vector<unsigned char>& o) {
    o.push_back(0x0A); nbtString(o, "");
}

static bool buildServersDat(std::vector<unsigned char>& out) {
    out.clear();
    compoundStart(out);                       // root compound
    out.push_back(0x09);                      // TAG_List
    nbtString(out, "servers");                // name
    out.push_back(0x0A);                      // element type = TAG_Compound
    nbtInt32(out, 1);                         // length = 1
    compoundStart(out);                       // element compound
    tagString(out, "name", "Hypixel");
    tagString(out, "ip", "mc.hypixel.net");
    tagByte(out, "hideAddress", 0);
    out.push_back(0x00);                      // TAG_End (element)
    out.push_back(0x00);                      // TAG_End (root)
    return true;
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type != EVENT_GAME_LAUNCHING) return 0;

    const char* gd = g_host->get_setting("gameDir");
    if (!gd || !*gd) return 0;
    std::wstring path = u8w(std::string(gd)) + L"\\servers.dat";

    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return 0;  // already exists

    std::vector<unsigned char> data;
    if (!buildServersDat(data)) return 0;
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    WriteFile(hf, &data[0], (DWORD)data.size(), &written, NULL);
    CloseHandle(hf);

    g_host->log("INFO", "\u5DF2\u751F\u6210\u670D\u52A1\u5668\u5217\u8868");
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->log("INFO", "\u670D\u52A1\u5668\u9884\u7F6E\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "server_join"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
