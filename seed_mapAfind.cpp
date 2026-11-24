// ============================================================================
//  seed_mapAfind.dll v3.0 - Minecraft seed map & finder (cubiomes powered)
//  - Chinese labels for biomes / structures / ores
//  - Cached offscreen bitmap (drag = BitBlt, no re-sample) for high FPS
//  - Height slider (underground biomes), biome viability check, ore finder,
//    measure tool, pins, world border, command-line window
// ============================================================================

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <fstream>
#include "plugin_api.h"

#include "generator.h"
#include "finders.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;

// ============================================================================
//  Chinese names
// ============================================================================
static const char* biomeName(int id) {
    switch (id) {
    case 0: return "\u6D77\u6D0B";
    case 1: return "\u5E73\u539F";
    case 2: return "\u6C99\u6F20";
    case 3: return "\u98CE\u88AD\u4E18\u9675";
    case 4: return "\u68EE\u6797";
    case 5: return "\u9488\u53F6\u6797";
    case 6: return "\u6CBC\u6CFD";
    case 7: return "\u6CB3\u6D41";
    case 8: return "\u4E0B\u754C\u8352\u5730";
    case 9: return "\u672B\u5730";
    case 10: return "\u51BB\u6D0B";
    case 11: return "\u51BB\u6CB3";
    case 12: return "\u96EA\u539F";
    case 13: return "\u96EA\u5C71";
    case 14: return "\u8611\u83C7\u5C9B";
    case 16: return "\u6C99\u6EE9";
    case 17: return "\u6C99\u6F20\u4E18\u9675";
    case 18: return "\u68EE\u6797\u4E18\u9675";
    case 21: return "\u4E1B\u6797";
    case 22: return "\u4E1B\u6797\u4E18\u9675";
    case 23: return "\u4E1B\u6797\u8FB9\u7F18";
    case 24: return "\u6DF1\u6D77";
    case 25: return "\u77F3\u5CB8";
    case 26: return "\u79EF\u96EA\u6C99\u6EE9";
    case 27: return "\u6866\u6728\u68EE\u6797";
    case 29: return "\u9ED1\u68EE\u6797";
    case 30: return "\u79EF\u96EA\u9488\u53F6\u6797";
    case 32: return "\u5DE8\u578B\u9488\u53F6\u6797";
    case 34: return "\u7E41\u8302\u5C71\u5730";
    case 35: return "\u70ED\u5E26\u8349\u539F";
    case 36: return "\u70ED\u5E26\u9AD8\u539F";
    case 37: return "\u6076\u5730";
    case 38: return "\u7E41\u8302\u6076\u5730\u9AD8\u539F";
    case 39: return "\u6076\u5730\u9AD8\u539F";
    case 42: return "\u672B\u5730\u9AD8\u5730";
    case 44: return "\u6696\u6C34\u6D77\u6D0B";
    case 45: return "\u6E29\u6C34\u6D77\u6D0B";
    case 46: return "\u51B7\u6C34\u6D77\u6D0B";
    case 168: return "\u7AF9\u6797";
    case 170: return "\u7075\u9B42\u6C99\u5CE1\u8C37";
    case 171: return "\u7EBB\u7EA2\u68EE\u6797";
    case 172: return "\u8BE1\u5F02\u68EE\u6797";
    case 173: return "\u7384\u6B66\u5CA9\u4E09\u89D2\u6D32";
    case 174: return "\u6EB6\u6D1E";
    case 175: return "\u7E41\u8302\u6D1E\u7A74";
    case 177: return "\u8349\u7538";
    case 178: return "\u6811\u6797";
    case 179: return "\u79EF\u96EA\u5761\u5730";
    case 180: return "\u5C16\u5CED\u5C71\u5CF0";
    case 181: return "\u51B0\u51BB\u5C71\u5CF0";
    case 182: return "\u77F3\u5CF0";
    case 183: return "\u6DF1\u6697\u4E4B\u57DF";
    case 184: return "\u7EA2\u6811\u6797\u6CBC\u6CFD";
    case 185: return "\u6A31\u82B1\u6811\u6797";
    case 186: return "\u82CD\u767D\u4E4B\u56ED";
    default: return "";
    }
}

struct StructEntry { int type; const char* en; const char* zh; };
static const StructEntry g_structs[] = {
    {Village, "Village", "\u6751\u5E84"},
    {Desert_Pyramid, "Desert Pyramid", "\u6C99\u6F20\u795E\u6BBF"},
    {Jungle_Temple, "Jungle Temple", "\u4E1B\u6797\u795E\u5E99"},
    {Swamp_Hut, "Swamp Hut", "\u6CBC\u6CFD\u5C0F\u5C4B"},
    {Igloo, "Igloo", "\u96EA\u5C4B"},
    {Monument, "Ocean Monument", "\u6D77\u5E95\u795E\u6BBF"},
    {Mansion, "Woodland Mansion", "\u6797\u5730\u5E9C\u90B8"},
    {Outpost, "Pillager Outpost", "\u63A0\u593A\u8005\u524D\u54E8\u7AD9"},
    {Ancient_City, "Ancient City", "\u8FDC\u53E4\u57CE\u5E02"},
    {Mineshaft, "Mineshaft", "\u5E9F\u5F03\u77FF\u4E95"},
    {Fortress, "Nether Fortress", "\u4E0B\u754C\u8981\u585E"},
    {Bastion, "Bastion Remnant", "\u5821\u5792\u9057\u8FF9"},
    {End_City, "End City", "\u672B\u5730\u57CE"},
    {Trail_Ruins, "Trail Ruins", "\u53E4\u8FF9\u5E9F\u589F"},
    {Trial_Chambers, "Trial Chambers", "\u8BD5\u70BC\u5BC6\u5BA4"},
    {Geode, "Amethyst Geode", "\u7D2B\u6C34\u6676\u6676\u6D1E"},
    {Ruined_Portal, "Ruined Portal", "\u5E9F\u5F03\u4F20\u9001\u95E8"},
    {Shipwreck, "Shipwreck", "\u6C89\u8239"},
    {Ocean_Ruin, "Ocean Ruin", "\u6D77\u5E95\u5E9F\u589F"},
    {Treasure, "Buried Treasure", "\u57CB\u85CF\u7684\u5B9D\u85CF"},
    {Desert_Well, "Desert Well", "\u6C99\u6F20\u6C34\u4E95"}
};
static const int g_structCount = sizeof(g_structs)/sizeof(g_structs[0]);

struct OreEntry { const char* zh; const char* en; int yMin, yMax, recY, veins; int r, g, b; };
static const OreEntry g_ores[] = {
    {"\u94BB\u77F3", "diamond", -64, 16, -59, 1, 0x4A, 0xED, 0xD2},
    {"\u8FDC\u53E4\u6B8B\u9AB8", "ancient_debris", 8, 119, 15, 2, 0x6B, 0x42, 0x3A},
    {"\u94C1", "iron", -64, 320, 16, 10, 0xD8, 0xAF, 0x93},
    {"\u91D1", "gold", -64, 32, 32, 2, 0xFC, 0xEE, 0x4B},
    {"\u94DC", "copper", -16, 112, 48, 6, 0xE0, 0x77, 0x56},
    {"\u7EA2\u77F3", "redstone", -64, -32, -59, 8, 0xAA, 0x00, 0x00},
    {"\u9752\u91D1\u77F3", "lapis", -64, 64, 0, 2, 0x21, 0x4E, 0xB6},
    {"\u7164\u70AD", "coal", 0, 320, 96, 20, 0x2E, 0x2E, 0x2E},
    {"\u7EFF\u5B9D\u77F3", "emerald", -16, 320, 128, 3, 0x17, 0xDD, 0x62},
    {"\u4E0B\u754C\u77F3\u82F1", "nether_quartz", 10, 117, 60, 16, 0xEA, 0xE5, 0xDE}
};
static const int g_oreCount = sizeof(g_ores)/sizeof(g_ores[0]);

static uint32_t g_biomeColor[256];
static void initBiomeColors() {
    for (int i = 0; i < 256; i++) g_biomeColor[i] = 0x808080;
    struct B { int id, r, g, b; };
    static const B t[] = {
        {0,0x00,0x00,0x70},{1,0x8D,0xB3,0x60},{2,0xFA,0x94,0x18},{3,0x60,0x60,0x60},
        {4,0x05,0x66,0x21},{5,0x0B,0x66,0x59},{6,0x07,0xF9,0xB2},{7,0x00,0x00,0xFF},
        {8,0xBF,0x3B,0x3B},{9,0x80,0x80,0xFF},{10,0x70,0x70,0xD6},{11,0xA0,0xA0,0xFF},
        {12,0xFF,0xFF,0xFF},{13,0xA0,0xA0,0xA0},{14,0xFF,0x00,0xFF},{16,0xFA,0xDE,0x55},
        {17,0xD2,0x5F,0x12},{18,0x22,0x55,0x1C},{19,0x16,0x39,0x33},{21,0x53,0x7B,0x09},
        {22,0x2C,0x42,0x05},{23,0x62,0x8B,0x17},{24,0x00,0x00,0x30},{25,0xA2,0xA2,0x84},
        {26,0xFA,0xF0,0xC0},{27,0x30,0x74,0x44},{28,0x1F,0x5F,0x32},{29,0x40,0x51,0x1A},
        {30,0x31,0x55,0x4A},{32,0x59,0x66,0x51},{34,0x50,0x6A,0x4A},{35,0xBD,0xB2,0x5F},
        {36,0xA7,0x9D,0x64},{37,0xD9,0x45,0x15},{38,0xB0,0x97,0x65},{39,0xCA,0x8C,0x65},
        {42,0xA0,0xA0,0xFF},{44,0x00,0x00,0xAC},{45,0x00,0x00,0x90},{46,0x20,0x20,0x70},
        {168,0x76,0x8E,0x14},{170,0x5E,0x38,0x30},{171,0xDD,0x08,0x06},{172,0x49,0x90,0x7B},
        {173,0x4B,0x4B,0x4B},{174,0x8B,0x6B,0x4B},{175,0x3A,0x7A,0x3A},{177,0x83,0xBB,0x6D},
        {178,0x3A,0x4A,0x2C},{179,0xE7,0xF0,0xF5},{180,0x9A,0xA6,0xA6},{181,0xDD,0xEB,0xEE},
        {182,0x8C,0x8C,0x8C},{183,0x0A,0x14,0x14},{184,0x2C,0x5A,0x3D},{185,0xF6,0xB4,0xD0},
        {186,0x7A,0x7F,0x72}
    };
    for (size_t i = 0; i < sizeof(t)/sizeof(t[0]); i++) g_biomeColor[t[i].id] = (t[i].r<<16)|(t[i].g<<8)|t[i].b;
}

// ============================================================================
//  helpers
// ============================================================================
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
    wchar_t buf[MAX_PATH]; DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return "";
    std::wstring p(buf, n); size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return "";
    return wUtf8(p.substr(0, pos));
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}
static std::mutex g_fileMtx;
static void appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_fileMtx);
    std::ofstream f((dllDir() + "\\seed_log.txt").c_str(), std::ios::app);
    if (f.is_open()) { time_t t = time(NULL); struct tm* tm = localtime(&t); char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm); f << "[" << ts << "] " << line << "\n"; f.close(); }
}

// ============================================================================
//  Map state (cached offscreen bitmap)
// ============================================================================
static HWND g_mapWin = NULL;
static HWND g_seedEdit = NULL, g_verCombo = NULL, g_dimCombo = NULL, g_structCombo = NULL;
static HWND g_chkStruct = NULL, g_chkSlime = NULL, g_heightSlider = NULL, g_heightLabel = NULL;
static HWND g_lblStatus = NULL;

static int g_mc = MC_NEWEST, g_dim = DIM_OVERWORLD, g_structType = Village;
static int64_t g_seed = 0;
static int g_heightY = 64;
static bool g_showStruct = true, g_showSlime = false;
static int64_t g_centerX = 0, g_centerZ = 0;
static double g_zoom = 1.0;

static HBITMAP g_cacheBmp = NULL;
static HDC g_cacheDC = NULL;
static int g_cacheW = 0, g_cacheH = 0;
static double g_cacheCX = 0, g_cacheCZ = 0, g_cacheZoom = 0;
static int g_cacheMc = -1, g_cacheDim = -1, g_cacheStruct = -1, g_cacheY = -999;
static int64_t g_cacheSeed = 0;
static bool g_cacheSlime = false;

struct Pin { int64_t x, z; };
static std::vector<Pin> g_pins;
static std::vector<int64_t> g_measurePts;
static bool g_measuring = false;
static bool g_showBorder = false;
static int64_t g_borderX = 0, g_borderZ = 0, g_borderR = 2000;

static int64_t parseSeed(const std::string& s) {
    std::string t = trim(s);
    bool digit = !t.empty();
    for (size_t i = 0; i < t.size(); i++) { if (i==0 && t[i]=='-') continue; if (!isdigit((unsigned char)t[i])) { digit=false; break; } }
    if (digit && !t.empty() && !(t.size()==1 && t[0]=='-')) return _strtoi64(t.c_str(), NULL, 10);
    int32_t h = 0; for (size_t i = 0; i < t.size(); i++) h = (int32_t)(h*31 + (unsigned char)t[i]);
    return (int64_t)h;
}

static int mapScaleForZoom() {
    if (g_zoom < 0.25) return 64;
    if (g_zoom < 1.0) return 16;
    return 4;
}

static void rebuildCache() {
    RECT rc; GetClientRect(g_mapWin, &rc);
    int W = rc.right, H = rc.bottom; if (W <= 0 || H <= 0) return;
    if (g_cacheBmp) { DeleteObject(g_cacheBmp); DeleteDC(g_cacheDC); g_cacheBmp = NULL; g_cacheDC = NULL; }

    int cw = W * 2, ch = H * 2;      // cache bitmap size in screen pixels
    int scale = mapScaleForZoom();
    int sx = (int)(cw / (g_zoom * scale)) + 2;   // sample points
    int sz = (int)(ch / (g_zoom * scale)) + 2;
    if (sx < 2) sx = 2; if (sz < 2) sz = 2;
    int x0 = (int)(g_centerX / scale - sx / 2);
    int z0 = (int)(g_centerZ / scale - sz / 2);

    Generator gen;
    setupGenerator(&gen, g_mc, 0);
    applySeed(&gen, g_dim, (uint64_t)g_seed);
    int* biomes = (int*)malloc(sizeof(int) * sx * sz);
    if (!biomes) return;
    Range r; r.scale = scale; r.x = x0; r.z = z0; r.sx = sx; r.sz = sz; r.y = g_heightY; r.sy = 1;
    genBiomes(&gen, biomes, r);

    HDC hdc = GetDC(g_mapWin);

    // sample bitmap (sx x sz), 1 px = 1 sample = scale blocks
    BITMAPINFO bmi; memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sx; bmi.bmiHeader.biHeight = -sz;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    uint32_t* px = NULL;
    HBITMAP sampleBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&px, NULL, 0);
    if (!px) { ReleaseDC(g_mapWin, hdc); free(biomes); return; }
    for (int z = 0; z < sz; z++) for (int x = 0; x < sx; x++) px[z*sx+x] = g_biomeColor[biomes[z*sx+x] & 255];
    free(biomes);

    // structure markers on sample bitmap
    if (g_showStruct) {
        StructureConfig conf;
        if (getStructureConfig(g_structType, g_mc, &conf)) {
            int regB = (conf.regionSize ? conf.regionSize : 32) * 16;
            int halfW = (int)(W / g_zoom / 2), halfH = (int)(H / g_zoom / 2);
            int rx0 = (int)((g_centerX - halfW) / regB) - 1, rx1 = (int)((g_centerX + halfW) / regB) + 1;
            int rz0 = (int)((g_centerZ - halfH) / regB) - 1, rz1 = (int)((g_centerZ + halfH) / regB) + 1;
            for (int rx = rx0; rx <= rx1; rx++) for (int rz = rz0; rz <= rz1; rz++) {
                Pos p;
                if (getStructurePos(g_structType, g_mc, (uint64_t)g_seed, rx, rz, &p)) {
                    if (!isViableStructurePos(g_structType, &gen, p.x, p.z, 0)) continue;
                    int px2 = (int)(p.x / (double)scale - x0);
                    int py2 = (int)(p.z / (double)scale - z0);
                    if (px2 >= 0 && px2 < sx && py2 >= 0 && py2 < sz)
                        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
                            if (px2+dx>=0 && px2+dx<sx && py2+dy>=0 && py2+dy<sz)
                                px[(py2+dy)*sx + (px2+dx)] = 0xFF2828;
                }
            }
        }
    }
    if (g_showSlime && g_dim == DIM_OVERWORLD) {
        int halfW = (int)(W / g_zoom / 2), halfH = (int)(H / g_zoom / 2);
        int cx0 = (int)floor((g_centerX - halfW)/16.0), cx1 = (int)floor((g_centerX + halfW)/16.0);
        int cz0 = (int)floor((g_centerZ - halfH)/16.0), cz1 = (int)floor((g_centerZ + halfH)/16.0);
        for (int cx = cx0; cx <= cx1; cx++) for (int cz = cz0; cz <= cz1; cz++)
            if (isSlimeChunk((uint64_t)g_seed, cx, cz)) {
                int px2 = (int)(cx*16/(double)scale - x0), py2 = (int)(cz*16/(double)scale - z0);
                int szx = (int)(16.0/scale); if (szx < 1) szx = 1;
                for (int dy = 0; dy < szx; dy++) for (int dx = 0; dx < szx; dx++)
                    if (px2+dx>=0 && px2+dx<sx && py2+dy>=0 && py2+dy<sz) {
                        uint32_t& c = px[(py2+dy)*sx + (px2+dx)];
                        c = ((c & 0xFEFEFE) >> 1) | 0x006000;
                    }
            }
    }

    // sample bitmap -> cache bitmap (nearest-neighbour upscale to screen pixel size)
    HDC sampleDC = CreateCompatibleDC(hdc);
    SelectObject(sampleDC, sampleBmp);

    BITMAPINFO bmi2; memset(&bmi2, 0, sizeof(bmi2));
    bmi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi2.bmiHeader.biWidth = cw; bmi2.bmiHeader.biHeight = -ch;
    bmi2.bmiHeader.biPlanes = 1; bmi2.bmiHeader.biBitCount = 32; bmi2.bmiHeader.biCompression = BI_RGB;
    g_cacheBmp = CreateDIBSection(hdc, &bmi2, DIB_RGB_COLORS, NULL, NULL, 0);
    g_cacheDC = CreateCompatibleDC(hdc);
    SelectObject(g_cacheDC, g_cacheBmp);
    SetStretchBltMode(g_cacheDC, COLORONCOLOR);
    StretchBlt(g_cacheDC, 0, 0, cw, ch, sampleDC, 0, 0, sx, sz, SRCCOPY);

    DeleteDC(sampleDC); DeleteObject(sampleBmp);
    ReleaseDC(g_mapWin, hdc);

    g_cacheW = cw; g_cacheH = ch;
    g_cacheCX = g_centerX; g_cacheCZ = g_centerZ; g_cacheZoom = g_zoom;
    g_cacheMc = g_mc; g_cacheDim = g_dim; g_cacheStruct = g_structType; g_cacheY = g_heightY;
    g_cacheSeed = g_seed; g_cacheSlime = g_showSlime;
}

static void mapRender(HDC hdc) {
    RECT rc; GetClientRect(g_mapWin, &rc);
    int W = rc.right, H = rc.bottom; if (W <= 0 || H <= 0) return;
    bool need = g_cacheBmp == NULL || g_cacheZoom != g_zoom || g_cacheMc != g_mc || g_cacheDim != g_dim
        || g_cacheStruct != g_structType || g_cacheY != g_heightY || g_cacheSeed != g_seed || g_cacheSlime != g_showSlime;
    if (!need) {
        double dx = fabs(g_centerX - g_cacheCX) * g_zoom;
        double dy = fabs(g_centerZ - g_cacheCZ) * g_zoom;
        if (dx > W/2 || dy > H/2) need = true;
    }
    if (need) rebuildCache();
    if (!g_cacheBmp) return;
    int offX = (int)((g_centerX - g_cacheCX) * g_zoom) + g_cacheW/2 - W/2;
    int offY = (int)((g_centerZ - g_cacheCZ) * g_zoom) + g_cacheH/2 - H/2;
    BitBlt(hdc, 0, 0, W, H, g_cacheDC, offX, offY, SRCCOPY);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
    HGDIOBJ op = SelectObject(hdc, pen);
    for (size_t i = 0; i < g_pins.size(); i++) {
        int px = (int)((g_pins[i].x - g_centerX) * g_zoom + W/2);
        int py = (int)((g_pins[i].z - g_centerZ) * g_zoom + H/2);
        Ellipse(hdc, px-5, py-5, px+5, py+5);
    }
    if (g_measurePts.size() >= 4) {
        for (size_t i = 0; i + 3 < g_measurePts.size(); i += 2) {
            int x1 = (int)((g_measurePts[i] - g_centerX) * g_zoom + W/2);
            int z1 = (int)((g_measurePts[i+1] - g_centerZ) * g_zoom + H/2);
            int x2 = (int)((g_measurePts[i+2] - g_centerX) * g_zoom + W/2);
            int z2 = (int)((g_measurePts[i+3] - g_centerZ) * g_zoom + H/2);
            MoveToEx(hdc, x1, z1, NULL); LineTo(hdc, x2, z2);
        }
    }
    if (g_showBorder) {
        int x1 = (int)((g_borderX - g_borderR - g_centerX) * g_zoom + W/2);
        int z1 = (int)((g_borderZ - g_borderR - g_centerZ) * g_zoom + H/2);
        int x2 = (int)((g_borderX + g_borderR - g_centerX) * g_zoom + W/2);
        int z2 = (int)((g_borderZ + g_borderR - g_centerZ) * g_zoom + H/2);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x1, z1, x2, z2);
        SelectObject(hdc, ob);
    }
    SelectObject(hdc, op); DeleteObject(pen);
}

static void screenToWorld(LPARAM lp, int64_t& x, int64_t& z) {
    RECT rc; GetClientRect(g_mapWin, &rc);
    int W = rc.right, H = rc.bottom;
    x = g_centerX + (int64_t)((GET_X_LPARAM(lp) - W/2) / g_zoom);
    z = g_centerZ + (int64_t)((GET_Y_LPARAM(lp) - H/2) / g_zoom);
}

static void updateStatus(int64_t mx, int64_t mz) {
    if (!g_lblStatus) return;
    Generator gen; setupGenerator(&gen, g_mc, 0); applySeed(&gen, g_dim, (uint64_t)g_seed);
    int id = getBiomeAt(&gen, 1, (int)mx, g_heightY, (int)mz);
    const char* nm = biomeName(id);
    char buf[256];
    snprintf(buf, sizeof(buf), "X:%lld Z:%lld Y:%d \u7FA4\u7CFB:%s", (long long)mx, (long long)mz, g_heightY, nm[0]?nm:"?");
    SetWindowTextW(g_lblStatus, u8w(buf).c_str());
}

static LRESULT CALLBACK mapWinProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static bool dragging = false; static POINT last;
    switch (m) {
    case WM_CREATE: {
        HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
        int y = 6;
        CreateWindowExW(0, L"STATIC", L"\u79CD\u5B50:", WS_CHILD|WS_VISIBLE, 8, y+3, 45, 20, h, NULL, inst, NULL);
        g_seedEdit = CreateWindowExW(0, L"EDIT", L"0", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL, 55, y, 140, 22, h, (HMENU)101, inst, NULL);
        CreateWindowExW(0, L"STATIC", L"\u7248\u672C:", WS_CHILD|WS_VISIBLE, 205, y+3, 45, 20, h, NULL, inst, NULL);
        g_verCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|CBS_DROPDOWNLIST, 250, y, 70, 200, h, (HMENU)102, inst, NULL);
        const char* verNames[] = {"1.0","1.7","1.8","1.9","1.10","1.11","1.12","1.13","1.14","1.15","1.16","1.17","1.18","1.19","1.20","1.21","\u6700\u65B0"};
        static const int verIds[] = {MC_1_0,MC_1_7,MC_1_8,MC_1_9,MC_1_10,MC_1_11,MC_1_12,MC_1_13,MC_1_14,MC_1_15,MC_1_16,MC_1_17,MC_1_18,MC_1_19,MC_1_20,MC_1_21,MC_NEWEST};
        for (int i = 0; i < 17; i++) SendMessageW(g_verCombo, CB_ADDSTRING, 0, (LPARAM)u8w(verNames[i]).c_str());
        SendMessageW(g_verCombo, CB_SETCURSEL, 16, 0);
        CreateWindowExW(0, L"STATIC", L"\u7EF4\u5EA6:", WS_CHILD|WS_VISIBLE, 330, y+3, 45, 20, h, NULL, inst, NULL);
        g_dimCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|CBS_DROPDOWNLIST, 375, y, 90, 200, h, (HMENU)103, inst, NULL);
        SendMessageW(g_dimCombo, CB_ADDSTRING, 0, (LPARAM)L"\u4E3B\u4E16\u754C");
        SendMessageW(g_dimCombo, CB_ADDSTRING, 0, (LPARAM)L"\u4E0B\u754C");
        SendMessageW(g_dimCombo, CB_ADDSTRING, 0, (LPARAM)L"\u672B\u5730");
        SendMessageW(g_dimCombo, CB_SETCURSEL, 0, 0);
        y += 28;
        CreateWindowExW(0, L"STATIC", L"\u7ED3\u6784:", WS_CHILD|WS_VISIBLE, 8, y+3, 45, 20, h, NULL, inst, NULL);
        g_structCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|CBS_DROPDOWNLIST, 55, y, 130, 200, h, (HMENU)104, inst, NULL);
        for (int i = 0; i < g_structCount; i++) SendMessageW(g_structCombo, CB_ADDSTRING, 0, (LPARAM)u8w(g_structs[i].zh).c_str());
        SendMessageW(g_structCombo, CB_SETCURSEL, 0, 0);
        g_chkStruct = CreateWindowExW(0, L"BUTTON", L"\u7ED3\u6784\u6807\u8BB0", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 195, y+2, 100, 22, h, (HMENU)105, inst, NULL);
        SendMessageW(g_chkStruct, BM_SETCHECK, BST_CHECKED, 0);
        g_chkSlime = CreateWindowExW(0, L"BUTTON", L"\u53F2\u83B1\u59C6", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 300, y+2, 80, 22, h, (HMENU)106, inst, NULL);
        CreateWindowExW(0, L"STATIC", L"\u9AD8\u5EA6:", WS_CHILD|WS_VISIBLE, 390, y+3, 40, 20, h, NULL, inst, NULL);
        g_heightSlider = CreateWindowExW(0, L"msctls_trackbar32", L"", WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_AUTOTICKS, 430, y, 120, 24, h, (HMENU)107, inst, NULL);
        SendMessageW(g_heightSlider, TBM_SETRANGE, TRUE, MAKELPARAM(-64, 320));
        SendMessageW(g_heightSlider, TBM_SETPOS, TRUE, 64);
        g_heightLabel = CreateWindowExW(0, L"STATIC", L"Y:64", WS_CHILD|WS_VISIBLE, 555, y+3, 60, 20, h, NULL, inst, NULL);
        y += 30;
        CreateWindowExW(0, L"BUTTON", L"\u91CD\u7F6E", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 8, y, 50, 24, h, (HMENU)108, inst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u6D4B\u8DDD", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 64, y, 50, 24, h, (HMENU)109, inst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u56FE\u9489", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 120, y, 50, 24, h, (HMENU)110, inst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u4E16\u754C\u8FB9\u754C", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 176, y, 80, 24, h, (HMENU)111, inst, NULL);
        g_lblStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD|WS_VISIBLE, 260, y+4, 400, 20, h, (HMENU)112, inst, NULL);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 101 && HIWORD(w) == EN_CHANGE) { int l2 = GetWindowTextLengthW(g_seedEdit); std::wstring ws(l2,0); GetWindowTextW(g_seedEdit,&ws[0],l2+1); g_seed = parseSeed(wUtf8(ws)); InvalidateRect(h,NULL,FALSE); }
        if (id == 102) { int s = (int)SendMessageW(g_verCombo, CB_GETCURSEL,0,0); static const int vids[]={MC_1_0,MC_1_7,MC_1_8,MC_1_9,MC_1_10,MC_1_11,MC_1_12,MC_1_13,MC_1_14,MC_1_15,MC_1_16,MC_1_17,MC_1_18,MC_1_19,MC_1_20,MC_1_21,MC_NEWEST}; if (s>=0&&s<17) g_mc=vids[s]; InvalidateRect(h,NULL,FALSE); }
        if (id == 103) { int s = (int)SendMessageW(g_dimCombo, CB_GETCURSEL,0,0); g_dim = (s==1)?DIM_NETHER:(s==2?DIM_END:DIM_OVERWORLD); InvalidateRect(h,NULL,FALSE); }
        if (id == 104) { int s = (int)SendMessageW(g_structCombo, CB_GETCURSEL,0,0); if (s>=0&&s<g_structCount) g_structType=g_structs[s].type; InvalidateRect(h,NULL,FALSE); }
        if (id == 105) { g_showStruct = (SendMessageW(g_chkStruct,BM_GETCHECK,0,0)==BST_CHECKED); InvalidateRect(h,NULL,FALSE); }
        if (id == 106) { g_showSlime = (SendMessageW(g_chkSlime,BM_GETCHECK,0,0)==BST_CHECKED); InvalidateRect(h,NULL,FALSE); }
        if (id == 108) { g_centerX=g_centerZ=0; g_zoom=1.0; InvalidateRect(h,NULL,FALSE); }
        if (id == 109) { g_measuring=!g_measuring; g_measurePts.clear(); }
        if (id == 110) { Pin p; p.x=g_centerX; p.z=g_centerZ; g_pins.push_back(p); InvalidateRect(h,NULL,FALSE); }
        if (id == 111) { g_showBorder=!g_showBorder; g_borderX=g_centerX; g_borderZ=g_centerZ; InvalidateRect(h,NULL,FALSE); }
        return 0;
    }
    case WM_HSCROLL: {
        int pos = (int)SendMessageW(g_heightSlider, TBM_GETPOS, 0, 0);
        g_heightY = pos;
        char b[32]; snprintf(b,sizeof(b),"Y:%d",pos); SetWindowTextW(g_heightLabel, u8w(b).c_str());
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (g_measuring) { int64_t x, z; screenToWorld(l, x, z); g_measurePts.push_back(x); g_measurePts.push_back(z); InvalidateRect(h,NULL,FALSE); return 0; }
        dragging = true; GetCursorPos(&last); SetCapture(h); return 0;
    case WM_MOUSEMOVE: {
        int64_t mx, mz; screenToWorld(l, mx, mz);
        updateStatus(mx, mz);
        if (dragging) { POINT p; GetCursorPos(&p); int dx=p.x-last.x, dy=p.y-last.y; last=p;
            g_centerX -= (int64_t)(dx/g_zoom); g_centerZ -= (int64_t)(dy/g_zoom); InvalidateRect(h,NULL,FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: dragging=false; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: {
        double f = (GET_WHEEL_DELTA_WPARAM(w) > 0) ? 1.2 : 1/1.2;
        g_zoom *= f; if (g_zoom<0.02) g_zoom=0.02; if (g_zoom>64) g_zoom=64;
        InvalidateRect(h,NULL,FALSE); return 0;
    }
    case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(h,&ps); mapRender(hdc); EndPaint(h,&ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================================================================
//  Find window
// ============================================================================
static HWND g_findWin = NULL, g_findOut = NULL, g_findStructCombo = NULL, g_findOreCombo = NULL;
static HWND g_findCount = NULL, g_findRange = NULL, g_findStart = NULL;

static void findWorker(int type, int64_t start, int count, int rangeB) {
    StructureConfig conf; int mc = MC_NEWEST;
    if (!getStructureConfig(type, mc, &conf)) { if (g_findOut) SetWindowTextW(g_findOut, L"\u4E0D\u652F\u6301"); return; }
    int regB = (conf.regionSize?conf.regionSize:32)*16;
    int rr = (int)(rangeB/regB)+2;
    std::string out; int hits=0;
    for (int i=0;i<count && hits<200;i++) {
        int64_t seed = start+i; Pos best; best.x=0;best.z=0; int bd=0x7fffffff;
        for (int rx=-rr;rx<=rr;rx++) for (int rz=-rr;rz<=rr;rz++) {
            Pos p; if (getStructurePos(type,mc,(uint64_t)seed,rx,rz,&p)) {
                int d=(int)sqrt((double)p.x*p.x+(double)p.z*p.z);
                if (d<=rangeB && d<bd) { best=p; bd=d; }
            }
        }
        if (bd<=rangeB) { char l[128]; snprintf(l,sizeof(l),"seed %lld  ->  %d, %d (dist %d)\r\n",(long long)seed,best.x,best.z,bd); out+=l; hits++; }
    }
    if (!hits) out="\u672A\u627E\u5230\r\n";
    if (g_findOut) SetWindowTextW(g_findOut, u8w(out).c_str());
    appendLog("seedfind hits=" + std::to_string(hits));
}
static void oreInfo() {
    int s = (int)SendMessageW(g_findOreCombo, CB_GETCURSEL,0,0);
    if (s<0||s>=g_oreCount) return;
    const OreEntry& o = g_ores[s];
    char b[512];
    snprintf(b,sizeof(b),"%s (%s)\r\nY\u533A\u95F4: %d ~ %d\r\n\u63A8\u8350 Y: %d\r\n\u6BCF\u533A\u5757\u8109\u6570: %d\r\n(\u77FF\u7269\u4E3A\u7EDF\u8BA1\u9884\u6D4B, \u975E\u65B9\u5757\u7EA7\u786E\u5B9A)\r\n", o.zh, o.en, o.yMin, o.yMax, o.recY, o.veins);
    if (g_findOut) SetWindowTextW(g_findOut, u8w(b).c_str());
}
static LRESULT CALLBACK findWinProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
        int y=6;
        CreateWindowExW(0,L"STATIC",L"\u7ED3\u6784:",WS_CHILD|WS_VISIBLE,8,y+3,45,20,h,NULL,inst,NULL);
        g_findStructCombo = CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|CBS_DROPDOWNLIST,55,y,120,200,h,(HMENU)201,inst,NULL);
        for (int i=0;i<g_structCount;i++) SendMessageW(g_findStructCombo,CB_ADDSTRING,0,(LPARAM)u8w(g_structs[i].zh).c_str());
        SendMessageW(g_findStructCombo,CB_SETCURSEL,0,0);
        CreateWindowExW(0,L"STATIC",L"\u77FF\u7269:",WS_CHILD|WS_VISIBLE,185,y+3,45,20,h,NULL,inst,NULL);
        g_findOreCombo = CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|CBS_DROPDOWNLIST,230,y,120,200,h,(HMENU)207,inst,NULL);
        for (int i=0;i<g_oreCount;i++) SendMessageW(g_findOreCombo,CB_ADDSTRING,0,(LPARAM)u8w(g_ores[i].zh).c_str());
        SendMessageW(g_findOreCombo,CB_SETCURSEL,0,0);
        CreateWindowExW(0,L"BUTTON",L"\u77FF\u7269\u8BE6\u60C5",WS_CHILD|WS_VISIBLE|WS_TABSTOP,360,y,80,24,h,(HMENU)208,inst,NULL);
        y+=28;
        CreateWindowExW(0,L"STATIC",L"\u8D77\u59CB:",WS_CHILD|WS_VISIBLE,8,y+3,45,20,h,NULL,inst,NULL);
        g_findStart = CreateWindowExW(0,L"EDIT",L"0",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,55,y,90,22,h,(HMENU)202,inst,NULL);
        CreateWindowExW(0,L"STATIC",L"\u6570\u91CF:",WS_CHILD|WS_VISIBLE,155,y+3,45,20,h,NULL,inst,NULL);
        g_findCount = CreateWindowExW(0,L"EDIT",L"10000",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,200,y,70,22,h,(HMENU)203,inst,NULL);
        CreateWindowExW(0,L"STATIC",L"\u8303\u56F4:",WS_CHILD|WS_VISIBLE,280,y+3,45,20,h,NULL,inst,NULL);
        g_findRange = CreateWindowExW(0,L"EDIT",L"2000",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,325,y,70,22,h,(HMENU)204,inst,NULL);
        CreateWindowExW(0,L"BUTTON",L"\u5F00\u59CB\u67E5\u627E",WS_CHILD|WS_VISIBLE|WS_TABSTOP,405,y,80,24,h,(HMENU)205,inst,NULL);
        y+=30;
        g_findOut = CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,8,y,500,330,h,(HMENU)206,inst,NULL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w)==205) {
            int t=Village; int s=(int)SendMessageW(g_findStructCombo,CB_GETCURSEL,0,0); if(s>=0&&s<g_structCount)t=g_structs[s].type;
            auto gn=[](HWND h,int d){if(!h)return d;int l2=GetWindowTextLengthW(h);std::wstring w(l2,0);GetWindowTextW(h,&w[0],l2+1);int v=_wtoi(w.c_str());return v>0?v:d;};
            int count=gn(g_findCount,10000), range=gn(g_findRange,2000);
            int64_t start=0; if(g_findStart){int l2=GetWindowTextLengthW(g_findStart);std::wstring w(l2,0);GetWindowTextW(g_findStart,&w[0],l2+1);start=_strtoi64(wUtf8(w).c_str(),NULL,10);}
            SetWindowTextW(g_findOut,L"\u67E5\u627E\u4E2D...\r\n");
            std::thread th(findWorker,t,start,count,range); th.detach();
        }
        if (LOWORD(w)==208) oreInfo();
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ============================================================================
//  Command-line window
// ============================================================================
static HWND g_cmdWin = NULL, g_cmdOut = NULL, g_cmdIn = NULL;
static WNDPROC g_cmdOldEdit = NULL;

static void cmdAppend(const std::string& s) { if(!g_cmdOut)return; std::wstring w=u8w(s); int n=GetWindowTextLengthW(g_cmdOut); SendMessageW(g_cmdOut,EM_SETSEL,n,n); SendMessageW(g_cmdOut,EM_REPLACESEL,FALSE,(LPARAM)w.c_str()); }
static void cmdRun(const std::string& raw) {
    std::string line=trim(raw); cmdAppend("> "+line+"\r\n"); if(line.empty())return;
    std::string cmd=line, arg; size_t sp=line.find(' '); if(sp!=std::string::npos){cmd=line.substr(0,sp);arg=trim(line.substr(sp+1));}
    if (cmd=="help") cmdAppend("seed <\u503C>  \u8BBE\u7F6E\u79CD\u5B50\r\ncenter <x> <z>  \u8BBE\u7F6E\u4E2D\u5FC3\r\nzoom <\u503C>  \u8BBE\u7F6E\u7F29\u653E\r\nheight <y>  \u8BBE\u7F6E\u9AD8\u5EA6\r\nslime on|off\r\nborder on|off\r\n");
    else if (cmd=="seed") { g_seed=parseSeed(arg); InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\u79CD\u5B50\r\n"); }
    else if (cmd=="center") { long long x=0,z=0; sscanf(arg.c_str(),"%lld %lld",&x,&z); g_centerX=x;g_centerZ=z; InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\r\n"); }
    else if (cmd=="zoom") { double v=atof(arg.c_str()); if(v>0){g_zoom=v;} InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\r\n"); }
    else if (cmd=="height") { g_heightY=atoi(arg.c_str()); InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\r\n"); }
    else if (cmd=="slime") { g_showSlime=(arg=="on"); if(g_chkSlime)SendMessageW(g_chkSlime,BM_SETCHECK,g_showSlime?BST_CHECKED:BST_UNCHECKED,0); InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\r\n"); }
    else if (cmd=="border") { g_showBorder=(arg=="on"); InvalidateRect(g_mapWin,NULL,FALSE); cmdAppend("\u5DF2\u8BBE\u7F6E\r\n"); }
    else cmdAppend("\u672A\u77E5\u547D\u4EE4\r\n");
}
static LRESULT CALLBACK cmdEditProc(HWND h,UINT m,WPARAM w,LPARAM l){ if(m==WM_KEYDOWN&&w==VK_RETURN){int n=GetWindowTextLengthW(h);std::wstring ws(n,0);GetWindowTextW(h,&ws[0],n+1);SetWindowTextW(h,L"");cmdRun(wUtf8(ws));return 0;} return CallWindowProcW(g_cmdOldEdit,h,m,w,l); }
static LRESULT CALLBACK cmdWinProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CREATE: {
        HINSTANCE inst=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);
        g_cmdOut=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL,8,8,460,280,h,(HMENU)1,inst,NULL);
        g_cmdIn=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,8,296,390,24,h,(HMENU)2,inst,NULL);
        CreateWindowExW(0,L"BUTTON",L"\u6267\u884C",WS_CHILD|WS_VISIBLE|WS_TABSTOP,405,294,65,26,h,(HMENU)3,inst,NULL);
        g_cmdOldEdit=(WNDPROC)SetWindowLongPtrW(g_cmdIn,GWLP_WNDPROC,(LONG_PTR)cmdEditProc);
        cmdAppend("\u79CD\u5B50\u547D\u4EE4\u884C, \u8F93\u5165 help\r\n\r\n");
        return 0;
    }
    case WM_COMMAND: if(LOWORD(w)==3){int n=GetWindowTextLengthW(g_cmdIn);std::wstring ws(n,0);GetWindowTextW(g_cmdIn,&ws[0],n+1);SetWindowTextW(g_cmdIn,L"");cmdRun(wUtf8(ws));return 0;} break;
    case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

// ============================================================================
//  threads / buttons
// ============================================================================
static void regClass(const wchar_t* c, WNDPROC p){ WNDCLASSW wc; memset(&wc,0,sizeof(wc)); wc.lpfnWndProc=p; wc.hInstance=(HINSTANCE)GetModuleHandleW(NULL); wc.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=c; RegisterClassW(&wc); }
static void mapThread(){ regClass(L"GDMLSeedMapWnd",mapWinProc); g_mapWin=CreateWindowExW(0,L"GDMLSeedMapWnd",L"\u79CD\u5B50\u5730\u56FE",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,820,640,NULL,NULL,(HINSTANCE)GetModuleHandleW(NULL),NULL); MSG msg; while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
static void findThread(){ regClass(L"GDMLSeedFindWnd",findWinProc); g_findWin=CreateWindowExW(0,L"GDMLSeedFindWnd",L"\u79CD\u5B50\u67E5\u627E",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,530,440,NULL,NULL,(HINSTANCE)GetModuleHandleW(NULL),NULL); MSG msg; while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
static void cmdThread(){ regClass(L"GDMLSeedCmdWnd",cmdWinProc); g_cmdWin=CreateWindowExW(0,L"GDMLSeedCmdWnd",L"\u79CD\u5B50\u547D\u4EE4\u884C",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,480,360,NULL,NULL,(HINSTANCE)GetModuleHandleW(NULL),NULL); MSG msg; while(GetMessageW(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
static void showWnd(HWND w){ if(w){ShowWindow(w,SW_SHOW);SetForegroundWindow(w);} }
static int onMapBtn(const PluginEvent* e){(void)e;showWnd(g_mapWin);return 0;}
static int onFindBtn(const PluginEvent* e){(void)e;showWnd(g_findWin);return 0;}
static int onCmdBtn(const PluginEvent* e){(void)e;showWnd(g_cmdWin);return 0;}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    initBiomeColors();
    INITCOMMONCONTROLSEX icc; icc.dwSize = sizeof(icc); icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    if (host->add_button) {
        host->add_button("\u79CD\u5B50\u5730\u56FE", onMapBtn);
        host->add_button("\u79CD\u5B50\u67E5\u627E", onFindBtn);
        host->add_button("\u79CD\u5B50\u547D\u4EE4", onCmdBtn);
    }
    std::thread(mapThread).detach();
    std::thread(findThread).detach();
    std::thread(cmdThread).detach();
    host->log("INFO", "\u79CD\u5B50\u5730\u56FE/\u67E5\u627E\u63D2\u4EF6\u5DF2\u52A0\u8F7D (cubiomes v3)");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) {
    if (g_mapWin) PostMessageW(g_mapWin, WM_DESTROY, 0, 0);
    if (g_findWin) PostMessageW(g_findWin, WM_DESTROY, 0, 0);
    if (g_cmdWin) PostMessageW(g_cmdWin, WM_DESTROY, 0, 0);
    g_host = 0;
}
PLUGIN_EXPORT const char* plugin_name(void) { return "seed_mapAfind"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "3.0"; }
