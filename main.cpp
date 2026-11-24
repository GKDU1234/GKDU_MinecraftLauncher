// ============================================================================
//  Minecraft third-party launcher (single-file C++14)
//  ----------------------------------------------------------------------------
//  Build: D:\Dev-Cpp\MinGW64\bin\g++.exe -O2 -static -std=c++14 -mwindows \
//           -o MinecraftLauncher.exe minecraft_launcher.cpp -lwinhttp
//
//  Runtime: Windows 7 / 8 / 8.1 / 10 / 11 (x64)
//  Features: GUI (custom colors), official (Microsoft 4-step) & offline login, game launch,
//        vanilla/Forge/NeoForge/Fabric install, mod download (Modrinth/direct link),
//        full assets objects download, DLL plugin system.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601   // Target Win7+
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>

// ============================================================================
//  1. Basic utilities: encoding / file / directory
// ============================================================================

static std::wstring utf8tow(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string wtoutf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a[a.size() - 1] == '\\' || a[a.size() - 1] == '/') return a + b;
    return a + "\\" + b;
}

static std::string appDataDir() {
    char* a = getenv("APPDATA");
    if (a && *a) return std::string(a);
    return "C:\\";
}

static void mkdirs(const std::string& p) {
    if (p.empty()) return;
    std::string cur;
    for (size_t i = 0; i < p.size(); i++) {
        char ch = p[i];
        if (ch == '/' || ch == '\\') {
            if (!cur.empty()) CreateDirectoryW(utf8tow(cur).c_str(), NULL);
            cur += ch;
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) CreateDirectoryW(utf8tow(cur).c_str(), NULL);
}

// Extract parent directory (empty if no separator)
static std::string parentDir(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
}

static FILE* wopen(const std::string& p, const char* mode) {
    return _wfopen(utf8tow(p).c_str(), utf8tow(mode).c_str());
}

static bool fileExists(const std::string& p) {
    DWORD a = GetFileAttributesW(utf8tow(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dirExists(const std::string& p) {
    DWORD a = GetFileAttributesW(utf8tow(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static long long fileSize(const std::string& p) {
    FILE* f = wopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long long n = ftell(f);
    fclose(f);
    return n;
}

static bool readFile(const std::string& p, std::string& out) {
    FILE* f = wopen(p, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    if (n > 0) fread(&out[0], 1, n, f);
    fclose(f);
    return true;
}
static bool writeFile(const std::string& p, const std::string& data) {
    mkdirs(parentDir(p));
    FILE* f = wopen(p, "wb");
    if (!f) return false;
    if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}
static bool writeBytes(const std::string& p, const std::vector<unsigned char>& d) {
    mkdirs(parentDir(p));
    FILE* f = wopen(p, "wb");
    if (!f) return false;
    if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    return true;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::string lowerStr(std::string s) {
    for (size_t i = 0; i < s.size(); i++) s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

static std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================================
//  2. MD5 (for offline account UUID v3)
// ============================================================================

static uint32_t rol32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5Block(uint32_t st[4], const unsigned char* p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const int S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    uint32_t M[16];
    for (int i = 0; i < 16; i++) M[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
        ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t tmp = d;
        d = c; c = b;
        b = b + rol32(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

static std::string md5Hex(const std::string& data) {
    uint32_t st[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    size_t len = data.size();
    std::vector<unsigned char> buf((len + 72) & ~63ull, 0);
    memcpy(buf.data(), data.data(), len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[buf.size() - 8 + i] = (unsigned char)(bits >> (i * 8));
    for (size_t off = 0; off < buf.size(); off += 64) md5Block(st, buf.data() + off);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            unsigned char byte = (unsigned char)(st[i] >> (j * 8));
            out += hex[byte >> 4];
            out += hex[byte & 0xf];
        }
    }
    return out;
}

// Offline UUID = MD5("OfflinePlayer:"+name) formatted as UUID v3
static std::string offlineUuid(const std::string& name) {
    std::string h = md5Hex("OfflinePlayer:" + name);
    h[12] = '3';                 // version 3
    h[16] = '8';                 // variant 10xx
    if (h[16] < '8' || h[16] > 'b') h[16] = '8';
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" +
        h.substr(16, 4) + "-" + h.substr(20, 12);
}

// ============================================================================
//  3. Minimal JSON parser
// ============================================================================

class Json {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } t;
    bool b; double n; std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;
    Json() : t(NUL), b(false), n(0) {}

    bool isNull() const { return t == NUL; }
    bool isObj()  const { return t == OBJ; }
    bool isArr()  const { return t == ARR; }
    bool isStr()  const { return t == STR; }
    bool has(const std::string& k) const { return t == OBJ && o.count(k) != 0; }
    const Json& get(const std::string& k) const {
        static Json nul;
        if (t == OBJ) { std::map<std::string, Json>::const_iterator it = o.find(k); if (it != o.end()) return it->second; }
        return nul;
    }
    const Json& at(size_t i) const {
        static Json nul;
        if (t == ARR && i < a.size()) return a[i];
        return nul;
    }
    size_t size() const { return t == ARR ? a.size() : 0; }
    std::string str(const std::string& d = "") const { return t == STR ? s : d; }
    double num(double d = 0) const { return t == NUM ? n : d; }
    long long integer(long long d = 0) const { return t == NUM ? (long long)n : d; }
    bool boolean(bool d = false) const { return t == BOOL ? b : d; }
};

struct JP { const char* p; const char* e; };

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

static Json jparse(JP& j);

static void jskip(JP& j) {
    while (j.p < j.e && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r')) j.p++;
}

static std::string jstring(JP& j) {
    jskip(j);
    if (j.p >= j.e || *j.p != '"') return "";
    j.p++;
    std::string out;
    while (j.p < j.e) {
        unsigned char c = (unsigned char)*j.p++;
        if (c == '"') break;
        if (c == '\\') {
            if (j.p >= j.e) break;
            char e = *j.p++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                for (int i = 0; i < 4 && j.p < j.e; i++) { cp <<= 4; cp |= (uint32_t)hexVal(*j.p++); }
                if (cp >= 0xD800 && cp <= 0xDBFF && j.p + 1 < j.e && j.p[0] == '\\' && j.p[1] == 'u') {
                    j.p += 2;
                    uint32_t lo = 0;
                    for (int i = 0; i < 4 && j.p < j.e; i++) { lo <<= 4; lo |= (uint32_t)hexVal(*j.p++); }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: out += e;
            }
        } else {
            out += (char)c;
        }
    }
    return out;
}

static Json jparse(JP& j) {
    Json v;
    jskip(j);
    if (j.p >= j.e) return v;
    char c = *j.p;
    if (c == '{') {
        j.p++; v.t = Json::OBJ; jskip(j);
        if (j.p < j.e && *j.p == '}') { j.p++; return v; }
        while (j.p < j.e) {
            jskip(j);
            if (j.p >= j.e || *j.p == '}') { j.p++; break; }
            std::string key = jstring(j);
            jskip(j);
            if (j.p < j.e && *j.p == ':') j.p++;
            v.o[key] = jparse(j);
            jskip(j);
            if (j.p < j.e && *j.p == ',') { j.p++; continue; }
        }
        return v;
    }
    if (c == '[') {
        j.p++; v.t = Json::ARR; jskip(j);
        if (j.p < j.e && *j.p == ']') { j.p++; return v; }
        while (j.p < j.e) {
            jskip(j);
            if (j.p >= j.e || *j.p == ']') { j.p++; break; }
            v.a.push_back(jparse(j));
            jskip(j);
            if (j.p < j.e && *j.p == ',') { j.p++; continue; }
        }
        return v;
    }
    if (c == '"') { v.t = Json::STR; v.s = jstring(j); return v; }
    if (c == 't') { v.t = Json::BOOL; v.b = true; j.p += 4; return v; }
    if (c == 'f') { v.t = Json::BOOL; v.b = false; j.p += 5; return v; }
    if (c == 'n') { v.t = Json::NUL; j.p += 4; return v; }
    v.t = Json::NUM;
    const char* s = j.p;
    if (*j.p == '-') j.p++;
    while (j.p < j.e && *j.p >= '0' && *j.p <= '9') j.p++;
    if (j.p < j.e && *j.p == '.') { j.p++; while (j.p < j.e && isdigit((unsigned char)*j.p)) j.p++; }
    if (j.p < j.e && (*j.p == 'e' || *j.p == 'E')) { j.p++; if (*j.p == '+' || *j.p == '-') j.p++; while (j.p < j.e && isdigit((unsigned char)*j.p)) j.p++; }
    v.n = strtod(s, NULL);
    return v;
}

static Json jsonParse(const std::string& txt) {
    JP j; j.p = txt.c_str(); j.e = txt.c_str() + txt.size();
    return jparse(j);
}

// Serialization (only for simple config / version json writing)
static std::string jsonEscape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default: o += c;
        }
    }
    return o;
}
static std::string jsonStringify(const Json& v) {
    switch (v.t) {
    case Json::NUL: return "null";
    case Json::BOOL: return v.b ? "true" : "false";
    case Json::NUM: {
        char buf[64];
        if (v.n == (long long)v.n) snprintf(buf, sizeof(buf), "%lld", (long long)v.n);
        else snprintf(buf, sizeof(buf), "%g", v.n);
        return buf;
    }
    case Json::STR: return "\"" + jsonEscape(v.s) + "\"";
    case Json::ARR: {
        std::string o = "[";
        for (size_t i = 0; i < v.a.size(); i++) { if (i) o += ","; o += jsonStringify(v.a[i]); }
        return o + "]";
    }
    case Json::OBJ: {
        std::string o = "{"; bool first = true;
        for (std::map<std::string, Json>::const_iterator it = v.o.begin(); it != v.o.end(); ++it) {
            if (!first) o += ",";
            first = false;
            o += "\"" + jsonEscape(it->first) + "\":" + jsonStringify(it->second);
        }
        return o + "}";
    }
    }
    return "null";
}

// ============================================================================
//  4. DEFLATE decompression (pure C++, for reading zip/jar)
// ============================================================================

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES (MAXLCODES + MAXDCODES)

struct Huff { short count[MAXBITS + 1]; short symbol[MAXCODES]; };
struct Infl { const unsigned char* in; size_t inlen; size_t inpos; int bitbuf; int bitcnt; };

static int getbits(Infl* s, int need) {
    int val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos >= s->inlen) return -1;
        val |= (int)(s->in[s->inpos++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)((unsigned int)val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1 << need) - 1));
}
static int hdecode(Infl* s, const Huff* h) {
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= MAXBITS; len++) {
        int b = getbits(s, 1);
        if (b < 0) return -10;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}
static int hbuild(Huff* h, const short* length, int n) {
    int len, symbol, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}
static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static int inflateBlock(Infl* s, Huff* lencode, Huff* distcode, std::vector<unsigned char>& out) {
    static const short lensbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const short distbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    for (;;) {
        int sym = hdecode(s, lencode);
        if (sym < 0) return sym;
        if (sym < 256) { out.push_back((unsigned char)sym); }
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -7;
            int eb = getbits(s, lext[sym]);
            if (eb < 0) return -7;
            int len = lensbase[sym] + eb;
            int dsym = hdecode(s, distcode);
            if (dsym < 0) return dsym;
            if (dsym >= 30) return -8;
            int de = getbits(s, dext[dsym]);
            if (de < 0) return -8;
            int dist = distbase[dsym] + de;
            if ((size_t)dist > out.size()) return -9;
            size_t from = out.size() - (size_t)dist;
            for (int i = 0; i < len; i++) out.push_back(out[from + (size_t)i]);
        }
    }
}

static int inflateRaw(const unsigned char* in, size_t inlen, std::vector<unsigned char>& out) {
    Infl s; s.in = in; s.inlen = inlen; s.inpos = 0; s.bitbuf = 0; s.bitcnt = 0;
    Huff lencode, distcode;
    int last = 0;
    while (!last) {
        last = getbits(&s, 1);
        int type = getbits(&s, 2);
        if (type == 0) {   // stored
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.inpos + 4 > s.inlen) return -1;
            unsigned int len = s.in[s.inpos] | (s.in[s.inpos + 1] << 8);
            unsigned int nlen = s.in[s.inpos + 2] | (s.in[s.inpos + 3] << 8);
            s.inpos += 4;
            if ((len ^ 0xFFFF) != nlen) return -2;
            if (s.inpos + len > s.inlen) return -3;
            size_t old = out.size();
            out.resize(old + len);
            memcpy(&out[old], s.in + s.inpos, len);
            s.inpos += len;
        }
        else if (type == 1) {  // fixed
            static Huff fl, fd;
            static int done = 0;
            if (!done) {
                short lengths[288];
                int i;
                for (i = 0; i < 144; i++) lengths[i] = 8;
                for (i = 144; i < 256; i++) lengths[i] = 9;
                for (i = 256; i < 280; i++) lengths[i] = 7;
                for (i = 280; i < 288; i++) lengths[i] = 8;
                hbuild(&fl, lengths, 288);
                short dl[MAXDCODES];
                for (i = 0; i < 30; i++) dl[i] = 5;
                hbuild(&fd, dl, 30);
                done = 1;
            }
            int r = inflateBlock(&s, &fl, &fd, out);
            if (r < 0) return r;
        }
        else if (type == 2) {  // dynamic
            int nlen = getbits(&s, 5) + 257;
            int ndist = getbits(&s, 5) + 1;
            int ncode = getbits(&s, 4) + 4;
            if (nlen > MAXLCODES || ndist > MAXDCODES) return -4;
            short lengths[MAXCODES];
            int i;
            for (i = 0; i < ncode; i++) lengths[order[i]] = (short)getbits(&s, 3);
            for (; i < 19; i++) lengths[order[i]] = 0;
            Huff lh;
            hbuild(&lh, lengths, 19);
            int index = 0;
            while (index < nlen + ndist) {
                int sym = hdecode(&s, &lh);
                if (sym < 0) return sym;
                if (sym < 16) lengths[index++] = (short)sym;
                else if (sym == 16) {
                    short prev = index > 0 ? lengths[index - 1] : 0;
                    int rep = getbits(&s, 2) + 3;
                    for (int k = 0; k < rep && index < nlen + ndist; k++) lengths[index++] = prev;
                }
                else if (sym == 17) {
                    int rep = getbits(&s, 3) + 3;
                    for (int k = 0; k < rep && index < nlen + ndist; k++) lengths[index++] = 0;
                }
                else if (sym == 18) {
                    int rep = getbits(&s, 7) + 11;
                    for (int k = 0; k < rep && index < nlen + ndist; k++) lengths[index++] = 0;
                }
                else return -5;
            }
            Huff lc, dc;
            hbuild(&lc, lengths, nlen);
            hbuild(&dc, lengths + nlen, ndist);
            int r = inflateBlock(&s, &lc, &dc, out);
            if (r < 0) return r;
        }
        else return -6;
    }
    return 0;
}

// ============================================================================
//  5. ZIP / JAR reading
// ============================================================================

static uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct ZipEntry {
    std::string name;
    uint16_t method;      // 0=store, 8=deflate
    uint32_t compSize, uncompSize, localOffset;
};

static bool readZipEntries(const std::string& zipPath, std::vector<ZipEntry>& entries) {
    FILE* f = wopen(zipPath, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long long fsz = ftell(f);
    if (fsz < 22) { fclose(f); return false; }
    long long searchFrom = fsz - 65558;
    if (searchFrom < 0) searchFrom = 0;
    fseek(f, (long)searchFrom, SEEK_SET);
    std::vector<unsigned char> tail((size_t)(fsz - searchFrom));
    if (!tail.empty()) fread(tail.data(), 1, tail.size(), f);
    long long eocdPos = -1;
    for (long long i = (long long)tail.size() - 22; i >= 0; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) { eocdPos = searchFrom + i; break; }
    }
    if (eocdPos < 0) { fclose(f); return false; }
    fseek(f, (long)eocdPos, SEEK_SET);
    unsigned char eocd[22];
    fread(eocd, 1, 22, f);
    uint32_t cdCount = le16(eocd + 10);
    uint32_t cdOffset = le32(eocd + 16);
    entries.clear();
    fseek(f, cdOffset, SEEK_SET);
    for (uint32_t i = 0; i < cdCount; i++) {
        unsigned char hdr[46];
        if (fread(hdr, 1, 46, f) != 46) break;
        if (le32(hdr) != 0x02014b50) break;
        ZipEntry e;
        e.method = le16(hdr + 10);
        e.compSize = le32(hdr + 20);
        e.uncompSize = le32(hdr + 24);
        uint16_t nameLen = le16(hdr + 28), extraLen = le16(hdr + 30), commentLen = le16(hdr + 32);
        e.localOffset = le32(hdr + 42);
        std::vector<char> name(nameLen + 1, 0);
        if (nameLen) fread(name.data(), 1, nameLen, f);
        e.name = name.data();
        entries.push_back(e);
        fseek(f, extraLen + commentLen, SEEK_CUR);
    }
    fclose(f);
    return true;
}

static bool readZipFile(const std::string& zipPath, const std::string& name, std::vector<unsigned char>& out) {
    std::vector<ZipEntry> entries;
    if (!readZipEntries(zipPath, entries)) return false;
    const ZipEntry* found = NULL;
    for (size_t i = 0; i < entries.size(); i++) if (entries[i].name == name) { found = &entries[i]; break; }
    if (!found) return false;
    FILE* f = wopen(zipPath, "rb");
    if (!f) return false;
    fseek(f, found->localOffset, SEEK_SET);
    unsigned char lh[30];
    fread(lh, 1, 30, f);
    if (le32(lh) != 0x04034b50) { fclose(f); return false; }
    uint16_t nameLen = le16(lh + 26), extraLen = le16(lh + 28);
    fseek(f, nameLen + extraLen, SEEK_CUR);
    std::vector<unsigned char> comp(found->compSize);
    if (found->compSize) fread(comp.data(), 1, comp.size(), f);
    fclose(f);
    out.clear();
    if (found->method == 0) { out = comp; return true; }
    if (found->method == 8) return inflateRaw(comp.data(), comp.size(), out) == 0;
    return false;
}

static bool extractZipAll(const std::string& zipPath, const std::string& outDir) {
    std::vector<ZipEntry> entries;
    if (!readZipEntries(zipPath, entries)) return false;
    FILE* f = wopen(zipPath, "rb");
    if (!f) return false;
    for (size_t i = 0; i < entries.size(); i++) {
        const ZipEntry& e = entries[i];
        if (e.name.empty()) continue;
        if (e.name[e.name.size() - 1] == '/') { mkdirs(join(outDir, e.name)); continue; }
        std::string full = join(outDir, e.name);
        fseek(f, e.localOffset, SEEK_SET);
        unsigned char lh[30];
        fread(lh, 1, 30, f);
        uint16_t nameLen = le16(lh + 26), extraLen = le16(lh + 28);
        fseek(f, nameLen + extraLen, SEEK_CUR);
        std::vector<unsigned char> comp(e.compSize);
        if (e.compSize) fread(comp.data(), 1, comp.size(), f);
        std::vector<unsigned char> data;
        if (e.method == 0) data = comp;
        else if (e.method == 8) { if (inflateRaw(comp.data(), comp.size(), data) != 0) continue; }
        else continue;
        writeBytes(full, data);
    }
    fclose(f);
    return true;
}

// ============================================================================
//  6. HTTP (WinHTTP) + download
// ============================================================================

struct HttpResult {
    bool ok;
    long status;
    std::string body;
};

static bool httpRequest(const std::string& method, const std::string& url,
    const std::map<std::string, std::string>& headers, const std::string& body,
    std::string& outBody, long* outStatus) {
    std::wstring wurl = utf8tow(url);
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[512], pathBuf[16384];
    uc.lpszHostName = hostBuf; uc.dwHostNameLength = 512;
    uc.lpszUrlPath = pathBuf; uc.dwUrlPathLength = 16384;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) { outBody = "url parse error"; return false; }
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring host(hostBuf, uc.dwHostNameLength);
    std::wstring path(pathBuf, uc.dwUrlPathLength);
    INTERNET_PORT port = uc.nPort;
    if (port == 0) port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET hSession = WinHttpOpen(L"MinecraftLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { outBody = "WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 60000);

    HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); outBody = "connect failed"; return false; }
    HINTERNET hReq = WinHttpOpenRequest(hConn, utf8tow(method).c_str(), path.c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); outBody = "open request failed"; return false; }

    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it) {
        WinHttpAddRequestHeaders(hReq, utf8tow(it->first + ": " + it->second).c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }

    bool sent;
    if (method == "POST" || method == "PUT") {
        sent = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) != FALSE;
    } else {
        sent = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) != FALSE;
    }
    if (!sent || !WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        outBody = "send/receive failed";
        return false;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
    if (outStatus) *outStatus = (long)status;

    std::string data;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(hReq, buf.data(), avail, &read)) break;
        data.append(buf.data(), read);
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    outBody = data;
    return status >= 200 && status < 400;
}

static bool httpGet(const std::string& url, std::string& out) {
    std::map<std::string, std::string> h;
    long st = 0;
    return httpRequest("GET", url, h, "", out, &st);
}
static bool httpGetJson(const std::string& url, Json& out) {
    std::string body;
    if (!httpGet(url, body)) return false;
    out = jsonParse(body);
    return true;
}

// Download file (with redirect + progress callback), returns success
static bool downloadFile(const std::string& url, const std::string& outPath,
    bool (*progressCb)(long long done, long long total, void* ctx), void* ctx, int maxRedirect = 6) {
    std::string curUrl = url;
    for (int rd = 0; rd <= maxRedirect; rd++) {
        std::wstring wurl = utf8tow(curUrl);
        URL_COMPONENTS uc;
        memset(&uc, 0, sizeof(uc));
        uc.dwStructSize = sizeof(uc);
        wchar_t hostBuf[512], pathBuf[16384];
        uc.lpszHostName = hostBuf; uc.dwHostNameLength = 512;
        uc.lpszUrlPath = pathBuf; uc.dwUrlPathLength = 16384;
        if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return false;
        bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        std::wstring host(hostBuf, uc.dwHostNameLength);
        std::wstring path(pathBuf, uc.dwUrlPathLength);
        INTERNET_PORT port = uc.nPort;
        if (port == 0) port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

        HINTERNET hSession = WinHttpOpen(L"MinecraftLauncher/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 15000, 15000, 60000, 120000);
        HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
        HINTERNET hReq = hConn ? WinHttpOpenRequest(hConn, L"GET", path.c_str(), NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : NULL;
        if (!hReq) { if (hConn) WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return false; }
        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
            !WinHttpReceiveResponse(hReq, NULL)) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
            return false;
        }
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &sz, NULL);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD locLen = 0;
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_LOCATION, NULL, NULL, &locLen, NULL);
            if (locLen > 0) {
                std::wstring loc(locLen, 0);
                WinHttpQueryHeaders(hReq, WINHTTP_QUERY_LOCATION, NULL, &loc[0], &locLen, NULL);
                curUrl = wtoutf8(loc);
                WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
                continue;
            }
        }
        if (status < 200 || status >= 400) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
            return false;
        }
        // Get total length
        DWORD contentLen = 0, clsz = sizeof(contentLen);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, NULL, &contentLen, &clsz, NULL);
        long long total = (long long)contentLen;

        mkdirs(parentDir(outPath));
        FILE* f = wopen(outPath, "wb");
        if (!f) { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return false; }
        long long done = 0;
        DWORD avail = 0;
        bool ok = true;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hReq, buf.data(), avail, &read) || read == 0) { ok = false; break; }
            fwrite(buf.data(), 1, read, f);
            done += read;
            if (progressCb && !progressCb(done, total, ctx)) { ok = false; break; }
        }
        fclose(f);
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
        return ok;
    }
    return false;
}

// ============================================================================
//  6b. Parallel downloader (speedup: multithreaded, skip existing files)
// ============================================================================

static void setStatus(const std::string& s);
static void setProgress(int cur, int max);

struct DlTask { std::string url; std::string target; };

struct ParallelState {
    std::atomic<int> done;
    std::atomic<int> failed;
    int total;
    std::string prefix;
};

static void pdWorker(std::deque<DlTask>* q, std::mutex* mtx, ParallelState* st) {
    for (;;) {
        DlTask t;
        {
            std::lock_guard<std::mutex> lk(*mtx);
            if (q->empty()) break;
            t = q->front();
            q->pop_front();
        }
        if (!(fileExists(t.target) && fileSize(t.target) > 0)) {
            if (!downloadFile(t.url, t.target, NULL, NULL)) {
                st->failed.fetch_add(1);
            }
        }
        int done = st->done.fetch_add(1) + 1;
        setStatus(st->prefix + ": " + std::to_string(done) + "/" + std::to_string(st->total));
        setProgress(done, st->total);
    }
}

// Parallel download task list (skip existing), returns failure count
static int parallelDownload(std::vector<DlTask>& tasks, int threads, const std::string& prefix) {
    if (tasks.empty()) return 0;
    ParallelState st;
    st.done.store(0);
    st.failed.store(0);
    st.total = (int)tasks.size();
    st.prefix = prefix;
    std::deque<DlTask> q;
    for (size_t i = 0; i < tasks.size(); i++) q.push_back(tasks[i]);
    std::mutex mtx;
    setProgress(0, st.total);
    int n = threads;
    if (n > (int)tasks.size()) n = (int)tasks.size();
    if (n < 1) n = 1;
    std::vector<std::thread> pool;
    for (int i = 0; i < n; i++) pool.push_back(std::thread(pdWorker, &q, &mtx, &st));
    for (size_t i = 0; i < pool.size(); i++) pool[i].join();
    setProgress(st.total, st.total);
    return st.failed.load();
}

// ============================================================================
//  7. Plugin interface (consistent with plugin_api.h)
// ============================================================================

#define MLAUNCHER_PLUGIN_API_VERSION 1

typedef enum PluginEventType {
    EVENT_LAUNCHER_START = 1,
    EVENT_VERSION_INSTALL_START,
    EVENT_VERSION_INSTALL_DONE,
    EVENT_DOWNLOAD_START,
    EVENT_DOWNLOAD_DONE,
    EVENT_GAME_LAUNCHING,
    EVENT_GAME_EXITED
} PluginEventType;

typedef struct PluginEvent {
    int type;
    const char* message;
    void* data;
    int result;
} PluginEvent;

typedef int (*PluginHookFn)(const PluginEvent* ev);

typedef struct PluginHost {
    int api_version;
    void (*log)(const char* level, const char* message);
    const char* (*get_setting)(const char* key);
    void (*set_setting)(const char* key, const char* value);
    int (*register_hook)(int event_type, PluginHookFn fn);
    int (*set_app_icon)(const char* icon_file_path);  /* load an .ico file and apply it as the launcher icon */
    int (*add_button)(const char* label, PluginHookFn fn);  /* register a toolbar button on the main window */
    int (*launch_game)(const char* version_id);  /* launch a version (empty = selected) */
    const char* (*list_plugins)(void);            /* comma-separated loaded plugin names */
    const char* (*get_launcher_dir)(void);        /* launcher data directory */
} PluginHost;

struct LoadedPlugin {
    HMODULE mod;
    int (*init)(const PluginHost*);
    void (*shutdown)(void);
    const char* (*name)(void);
    const char* (*version)(void);
    std::string path;
};

// A toolbar button registered by a plugin (shown at the bottom of the main window)
struct PluginButton {
    std::string label;
    PluginHookFn fn;
};

// ============================================================================
//  8. Global state / theme / config
// ============================================================================

struct Theme {
    COLORREF bg, panel, text, accent, accentText, button, buttonText, listBg, listText, logBg, logText, border;
};

struct AppState {
    // Directory
    std::string launcherDir;
    std::string gameDir;
    std::string javaPath;
    int maxMemoryMB;

    // Theme
    Theme theme;

    // Account
    std::string accountType;   // "offline" | "msa"
    std::string username;
    std::string uuid;
    std::string accessToken;
    std::string refreshToken;
    std::string userType;      // mojang / legacy

    // Version isolation: when true each version uses its own game dir (saves/mods/config isolated)
    bool versionIsolation;

    // Version manifest (remote)
    std::map<std::string, std::string> remoteVersions;  // id -> url

    // Download progress (worker writes, UI timer reads)
    std::mutex mtx;
    std::deque<std::string> logQueue;
    int progress, progressMax;
    std::string statusText;
    std::atomic<bool> busy;

    // Microsoft login
    std::string msCode, msVerifyUri;
    std::atomic<bool> msAck;

    // UI handles
    HWND hwnd;
    HWND editUser, editPass, comboVer, comboLoader, listInstalled, editJava, editMem;
    HWND logBox, statusLabel, editModDir;
    HFONT font;
    HBRUSH brushBg, brushPanel, brushList, brushEdit, brushLog;

    // Plugins
    std::vector<LoadedPlugin> plugins;
    std::vector<std::pair<int, PluginHookFn> > hooks;
    std::vector<PluginButton> pluginButtons;

    // Custom icon (set by plugins via set_app_icon)
    HICON customIcon;
    std::string customIconPath;

    // Custom mod install directory (empty = use default gameDir/mods or instance mods)
    std::string modDir;

    // Comma-separated offline account list (managed by multi_account plugin)
    std::string multiAccounts;

    AppState() : maxMemoryMB(2048), progress(0), progressMax(1), busy(false), msAck(false),
        versionIsolation(false),
        hwnd(NULL), editUser(NULL), editPass(NULL), comboVer(NULL), comboLoader(NULL),
        listInstalled(NULL), editJava(NULL), editMem(NULL), logBox(NULL), statusLabel(NULL),
        editModDir(NULL),
        font(NULL), brushBg(NULL), brushPanel(NULL), brushList(NULL), brushEdit(NULL), brushLog(NULL),
        customIcon(NULL) {
        // Initialize theme to the default palette so a saveConfig() before
        // loadConfig() (e.g. a plugin calling set_app_icon early) never writes
        // an all-black (uninitialized) theme.
        theme.bg = RGB(28, 30, 38);
        theme.panel = RGB(38, 41, 52);
        theme.text = RGB(230, 232, 238);
        theme.accent = RGB(64, 169, 255);
        theme.accentText = RGB(255, 255, 255);
        theme.button = RGB(52, 56, 70);
        theme.buttonText = RGB(225, 228, 235);
        theme.listBg = RGB(32, 35, 45);
        theme.listText = RGB(225, 228, 235);
        theme.logBg = RGB(22, 24, 31);
        theme.logText = RGB(190, 196, 208);
        theme.border = RGB(70, 75, 90);
    }
};

static AppState* G = NULL;

static Theme defaultTheme() {
    Theme t;
    t.bg = RGB(28, 30, 38);
    t.panel = RGB(38, 41, 52);
    t.text = RGB(230, 232, 238);
    t.accent = RGB(64, 169, 255);
    t.accentText = RGB(255, 255, 255);
    t.button = RGB(52, 56, 70);
    t.buttonText = RGB(225, 228, 235);
    t.listBg = RGB(32, 35, 45);
    t.listText = RGB(225, 228, 235);
    t.logBg = RGB(22, 24, 31);
    t.logText = RGB(190, 196, 208);
    t.border = RGB(70, 75, 90);
    return t;
}

static COLORREF colorFromJson(const Json& j, COLORREF def) {
    if (!j.isObj()) return def;
    int r = (int)j.get("r").integer(GetRValue(def));
    int g = (int)j.get("g").integer(GetGValue(def));
    int b = (int)j.get("b").integer(GetBValue(def));
    return RGB(r, g, b);
}
static Json colorToJson(COLORREF c) {
    Json o; o.t = Json::OBJ;
    Json r, g, b; r.t = Json::NUM; g.t = Json::NUM; b.t = Json::NUM;
    r.n = GetRValue(c); g.n = GetGValue(c); b.n = GetBValue(c);
    o.o["r"] = r; o.o["g"] = g; o.o["b"] = b;
    return o;
}

static std::string getConfigPath() { return join(G->launcherDir, "config.json"); }

static void logLine(const std::string& level, const std::string& msg) {
    if (!G) return;
    std::string line = "[" + level + "] " + msg;
    { std::lock_guard<std::mutex> lk(G->mtx); G->logQueue.push_back(line); }
    // Also write to log file
    static std::mutex fm;
    std::lock_guard<std::mutex> lk(fm);
    FILE* f = wopen(join(G->launcherDir, "launcher.log"), "ab");
    if (f) { std::string t = line + "\n"; fwrite(t.data(), 1, t.size(), f); fclose(f); }
}

static void logInfo(const std::string& m) { logLine("INFO", m); }
static void logErr(const std::string& m) { logLine("ERROR", m); }

static void setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(G->mtx);
    G->statusText = s;
}
static void setProgress(int cur, int max) {
    std::lock_guard<std::mutex> lk(G->mtx);
    G->progress = cur; G->progressMax = max > 0 ? max : 1;
}

// Forward declarations (defined later)
static void saveConfig();
static void launchGame(const std::string& versionId);
static std::string getSelectedInstalled();
static std::string getModsDir(const std::string& versionId);

// Plugin callback implementation
static void hostLog(const char* level, const char* msg) { logLine(level ? level : "PLUGIN", msg ? msg : ""); }
static const char* hostGetSetting(const char* key) {
    // Return persisted string, stored in static map (simplified)
    static std::map<std::string, std::string> cache;
    if (!key) return NULL;
    if (strcmp(key, "gameDir") == 0) { cache["gameDir"] = G->gameDir; return cache["gameDir"].c_str(); }
    if (strcmp(key, "username") == 0) { cache["username"] = G->username; return cache["username"].c_str(); }
    if (strcmp(key, "version") == 0) { cache["version"] = getSelectedInstalled(); return cache["version"].c_str(); }
    if (strcmp(key, "multiAccounts") == 0) { cache["multiAccounts"] = G->multiAccounts; return cache["multiAccounts"].c_str(); }
    if (strcmp(key, "modDir") == 0) { cache["modDir"] = getModsDir(""); return cache["modDir"].c_str(); }
    if (strcmp(key, "launcherDir") == 0) { cache["launcherDir"] = G->launcherDir; return cache["launcherDir"].c_str(); }
    return NULL;
}
static void hostSetSetting(const char* key, const char* value) {
    if (!key || !value) return;
    if (strcmp(key, "memory") == 0) G->maxMemoryMB = atoi(value);
    else if (strcmp(key, "username") == 0) { G->username = value; saveConfig(); }
    else if (strcmp(key, "multiAccounts") == 0) { G->multiAccounts = value; saveConfig(); }
}
static int hostRegisterHook(int type, PluginHookFn fn) {
    if (!fn) return -1;
    G->hooks.push_back(std::make_pair(type, fn));
    return 0;
}

// Load an icon from an .ico file. Supports default size and explicit 32/16 sizes.
static HICON loadIconFromFile(const std::string& path) {
    if (path.empty()) return NULL;
    std::wstring w = utf8tow(path);
    HICON icon = (HICON)LoadImageW(NULL, w.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!icon) icon = (HICON)LoadImageW(NULL, w.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    if (!icon) icon = (HICON)LoadImageW(NULL, w.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    return icon;
}

// Plugin service: set the launcher window/taskbar icon from an .ico file (UTF-8 path).
// Returns 0 on success. The custom icon is persisted to config.json and survives restarts.
static int hostSetAppIcon(const char* path) {
    if (!path || !*path) return -1;
    std::string p(path);
    HICON icon = loadIconFromFile(p);
    if (!icon) {
        logErr("\u52A0\u8F7D\u56FE\u6807\u5931\u8D25: " + p);
        return -2;
    }
    if (G->customIcon) DestroyIcon(G->customIcon);
    G->customIcon = icon;
    G->customIconPath = p;
    saveConfig();
    if (G->hwnd) {
        SendMessageW(G->hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(G->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
    logInfo("\u81EA\u5B9A\u4E49\u56FE\u6807\u5DF2\u5E94\u7528: " + p);
    return 0;
}

// Plugin service: register a toolbar button on the main window. Returns button
// index (>=0) or -1. The button is created in WM_CREATE; clicks invoke fn.
static int hostAddButton(const char* label, PluginHookFn fn) {
    if (!label || !fn) return -1;
    PluginButton b;
    b.label = label;
    b.fn = fn;
    G->pluginButtons.push_back(b);
    return (int)G->pluginButtons.size() - 1;
}

// Plugin service: launch a game version (empty = selected version) in background.
static int hostLaunchGame(const char* version_id) {
    std::string ver = version_id ? version_id : "";
    if (ver.empty()) ver = getSelectedInstalled();
    if (ver.empty()) return -1;
    std::thread t(launchGame, ver);
    t.detach();
    return 0;
}

// Plugin service: comma-separated list of loaded plugin names.
static const char* hostListPlugins() {
    static std::string s;
    s.clear();
    for (size_t i = 0; i < G->plugins.size(); i++) {
        if (i) s += ",";
        s += G->plugins[i].name ? G->plugins[i].name() : "?";
    }
    return s.c_str();
}

// Plugin service: launcher data directory.
static const char* hostGetLauncherDir() {
    return G->launcherDir.c_str();
}

static PluginHost makeHost() {
    PluginHost h;
    h.api_version = MLAUNCHER_PLUGIN_API_VERSION;
    h.log = hostLog;
    h.get_setting = hostGetSetting;
    h.set_setting = hostSetSetting;
    h.register_hook = hostRegisterHook;
    h.set_app_icon = hostSetAppIcon;
    h.add_button = hostAddButton;
    h.launch_game = hostLaunchGame;
    h.list_plugins = hostListPlugins;
    h.get_launcher_dir = hostGetLauncherDir;
    return h;
}

static void fireHook(int type, const char* msg, void* data, int result) {
    PluginEvent ev;
    ev.type = type; ev.message = msg; ev.data = data; ev.result = result;
    for (size_t i = 0; i < G->hooks.size(); i++) {
        if (G->hooks[i].first == type) G->hooks[i].second(&ev);
    }
}

static void loadPlugins() {
    std::string pdir = join(G->launcherDir, "plugins");
    mkdirs(pdir);
    WIN32_FIND_DATAW fd;
    std::wstring pattern = utf8tow(join(pdir, "*.dll"));
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    PluginHost host = makeHost();
    do {
        std::wstring name(fd.cFileName);
        std::string path = join(pdir, wtoutf8(name));
        HMODULE m = LoadLibraryW(utf8tow(path).c_str());
        if (!m) continue;
        LoadedPlugin p;
        p.mod = m;
        p.init = (int (*)(const PluginHost*))GetProcAddress(m, "plugin_init");
        p.shutdown = (void (*)(void))GetProcAddress(m, "plugin_shutdown");
        p.name = (const char* (*)(void))GetProcAddress(m, "plugin_name");
        p.version = (const char* (*)(void))GetProcAddress(m, "plugin_version");
        p.path = path;
        if (p.init) {
            if (p.init(&host) == 0) {
                G->plugins.push_back(p);
                const char* nm = p.name ? p.name() : wtoutf8(name).c_str();
                logInfo(std::string("\u63D2\u4EF6\u5DF2\u52A0\u8F7D: ") + (nm ? nm : "?"));
            } else {
                FreeLibrary(m);
            }
        } else {
            FreeLibrary(m);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void saveConfig() {
    Json o; o.t = Json::OBJ;
    Json t; t.t = Json::OBJ;
    t.o["bg"] = colorToJson(G->theme.bg);
    t.o["panel"] = colorToJson(G->theme.panel);
    t.o["text"] = colorToJson(G->theme.text);
    t.o["accent"] = colorToJson(G->theme.accent);
    t.o["accentText"] = colorToJson(G->theme.accentText);
    t.o["button"] = colorToJson(G->theme.button);
    t.o["buttonText"] = colorToJson(G->theme.buttonText);
    t.o["listBg"] = colorToJson(G->theme.listBg);
    t.o["listText"] = colorToJson(G->theme.listText);
    t.o["logBg"] = colorToJson(G->theme.logBg);
    t.o["logText"] = colorToJson(G->theme.logText);
    t.o["border"] = colorToJson(G->theme.border);
    o.o["theme"] = t;

    Json jp; jp.t = Json::STR; jp.s = G->javaPath; o.o["javaPath"] = jp;
    Json gd; gd.t = Json::STR; gd.s = G->gameDir; o.o["gameDir"] = gd;
    Json mem; mem.t = Json::NUM; mem.n = G->maxMemoryMB; o.o["maxMemoryMB"] = mem;
    Json at; at.t = Json::STR; at.s = G->accountType; o.o["accountType"] = at;
    Json un; un.t = Json::STR; un.s = G->username; o.o["username"] = un;
    Json uid; uid.t = Json::STR; uid.s = G->uuid; o.o["uuid"] = uid;
    Json rt; rt.t = Json::STR; rt.s = G->refreshToken; o.o["refreshToken"] = rt;
    Json vi; vi.t = Json::BOOL; vi.b = G->versionIsolation; o.o["versionIsolation"] = vi;
    Json cip; cip.t = Json::STR; cip.s = G->customIconPath; o.o["customIconPath"] = cip;
    Json md; md.t = Json::STR; md.s = G->modDir; o.o["modDir"] = md;
    Json ma; ma.t = Json::STR; ma.s = G->multiAccounts; o.o["multiAccounts"] = ma;
    writeFile(getConfigPath(), jsonStringify(o));
}

static void loadConfig() {
    std::string txt;
    G->theme = defaultTheme();
    G->gameDir = join(appDataDir(), ".minecraft");
    if (readFile(getConfigPath(), txt)) {
        Json o = jsonParse(txt);
        if (o.isObj()) {
            Json t = o.get("theme");
            if (t.isObj()) {
                Theme th = G->theme;
                th.bg = colorFromJson(t.get("bg"), th.bg);
                th.panel = colorFromJson(t.get("panel"), th.panel);
                th.text = colorFromJson(t.get("text"), th.text);
                th.accent = colorFromJson(t.get("accent"), th.accent);
                th.accentText = colorFromJson(t.get("accentText"), th.accentText);
                th.button = colorFromJson(t.get("button"), th.button);
                th.buttonText = colorFromJson(t.get("buttonText"), th.buttonText);
                th.listBg = colorFromJson(t.get("listBg"), th.listBg);
                th.listText = colorFromJson(t.get("listText"), th.listText);
                th.logBg = colorFromJson(t.get("logBg"), th.logBg);
                th.logText = colorFromJson(t.get("logText"), th.logText);
                th.border = colorFromJson(t.get("border"), th.border);
                G->theme = th;
            }
            G->javaPath = o.get("javaPath").str();
            G->gameDir = o.get("gameDir").str(G->gameDir);
            G->maxMemoryMB = (int)o.get("maxMemoryMB").integer(2048);
            G->accountType = o.get("accountType").str("offline");
            G->username = o.get("username").str();
            G->uuid = o.get("uuid").str();
            G->refreshToken = o.get("refreshToken").str();
            G->versionIsolation = o.get("versionIsolation").boolean(false);
            G->customIconPath = o.get("customIconPath").str();
            G->modDir = o.get("modDir").str();
            G->multiAccounts = o.get("multiAccounts").str();
        }
    }
    if (G->accountType.empty()) G->accountType = "offline";
    if (!G->uuid.empty() && G->accountType == "offline" && !G->username.empty()) G->uuid = offlineUuid(G->username);
}

// ============================================================================
//  9. Accounts: offline / Microsoft four-step chain
// ============================================================================

static void loginOffline(const std::string& name) {
    std::string n = trimStr(name);
    if (n.empty()) { logErr("\u79BB\u7EBF\u767B\u5F55: \u7528\u6237\u540D\u4E3A\u7A7A"); return; }
    G->accountType = "offline";
    G->username = n;
    G->uuid = offlineUuid(n);
    G->accessToken = "0";
    G->refreshToken = "";
    G->userType = "legacy";
    saveConfig();
    logInfo("\u79BB\u7EBF\u767B\u5F55\u6210\u529F: " + n + " (UUID " + G->uuid + ")");
}

// --- Microsoft OAuth device-code flow ---
static const std::string MS_CLIENT_ID = "00000000402b5328"; // Replace with your own Azure app client_id

static bool msDeviceCode(std::string& deviceCode, std::string& userCode, std::string& verifyUri, int& interval, int& expiresIn) {
    std::map<std::string, std::string> h;
    h["Content-Type"] = "application/x-www-form-urlencoded";
    std::string body = "client_id=" + MS_CLIENT_ID + "&scope=XboxLive.signin%20offline_access";
    std::string resp; long st = 0;
    if (!httpRequest("POST", "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode", h, body, resp, &st)) {
        logErr("\u5FAE\u8F6F\u767B\u5F55: \u83B7\u53D6\u8BBE\u5907\u7801\u5931\u8D25"); return false;
    }
    Json j = jsonParse(resp);
    if (j.has("error")) { logErr("\u5FAE\u8F6F\u767B\u5F55: " + j.get("error").str()); return false; }
    deviceCode = j.get("device_code").str();
    userCode = j.get("user_code").str();
    verifyUri = j.get("verification_uri").str();
    interval = (int)j.get("interval").integer(5);
    expiresIn = (int)j.get("expires_in").integer(900);
    return !deviceCode.empty();
}

static bool msPollToken(const std::string& deviceCode, std::string& accessToken, std::string& refreshToken) {
    std::map<std::string, std::string> h;
    h["Content-Type"] = "application/x-www-form-urlencoded";
    std::string body = "client_id=" + MS_CLIENT_ID +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=" + deviceCode;
    std::string resp; long st = 0;
    if (!httpRequest("POST", "https://login.microsoftonline.com/consumers/oauth2/v2.0/token", h, body, resp, &st)) return false;
    Json j = jsonParse(resp);
    if (j.has("access_token")) {
        accessToken = j.get("access_token").str();
        refreshToken = j.get("refresh_token").str();
        return true;
    }
    // authorization_pending / slow_down etc. -> keep waiting
    return false;
}

static bool msXboxChain(const std::string& msAccessToken, std::string& mcAccessToken, std::string& uuid, std::string& name) {
    // 2. Xbox Live authentication
    {
        std::map<std::string, std::string> h;
        h["Content-Type"] = "application/json";
        h["Accept"] = "application/json";
        std::string body = "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=" + msAccessToken + "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";
        std::string resp; long st = 0;
        if (!httpRequest("POST", "https://user.auth.xboxlive.com/user/authenticate", h, body, resp, &st)) { logErr("Xbox \u8BA4\u8BC1\u5931\u8D25"); return false; }
        Json j = jsonParse(resp);
        if (j.has("error")) { logErr("Xbox \u8BA4\u8BC1: " + j.get("error").str()); return false; }
        // Token and uhs are needed later
        std::string xblToken = j.get("Token").str();
        std::string uhs = j.get("DisplayClaims").get("xui").at(0).get("uhs").str();
        if (xblToken.empty()) { logErr("Xbox \u8BA4\u8BC1: \u65E0 Token"); return false; }

        // 3. XSTS authorization
        std::map<std::string, std::string> h2;
        h2["Content-Type"] = "application/json";
        h2["Accept"] = "application/json";
        std::string body2 = "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" + xblToken + "\"]},\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}";
        std::string resp2; long st2 = 0;
        if (!httpRequest("POST", "https://xsts.auth.xboxlive.com/xsts/authorize", h2, body2, resp2, &st2)) { logErr("XSTS \u6388\u6743\u5931\u8D25"); return false; }
        Json j2 = jsonParse(resp2);
        if (j2.has("XErr")) { logErr("XSTS \u9519\u8BEF(\u53EF\u80FD\u672A\u62E5\u6709\u6E38\u620F): " + j2.get("XErr").str()); return false; }
        std::string xstsToken = j2.get("Token").str();
        std::string uhs2 = j2.get("DisplayClaims").get("xui").at(0).get("uhs").str();

        // 4. Exchange for Minecraft token
        std::map<std::string, std::string> h3;
        h3["Content-Type"] = "application/json";
        h3["Accept"] = "application/json";
        std::string body3 = "{\"identityToken\":\"XBL3.0 x=" + uhs2 + ";" + xstsToken + "\"}";
        std::string resp3; long st3 = 0;
        if (!httpRequest("POST", "https://api.minecraftservices.com/authentication/login_with_xbox", h3, body3, resp3, &st3)) { logErr("Minecraft \u8BA4\u8BC1\u5931\u8D25"); return false; }
        Json j3 = jsonParse(resp3);
        if (j3.has("error")) { logErr("Minecraft \u8BA4\u8BC1: " + j3.get("error").str()); return false; }
        mcAccessToken = j3.get("access_token").str();

        // 5. Check game ownership
        std::map<std::string, std::string> h4;
        h4["Authorization"] = "Bearer " + mcAccessToken;
        std::string resp4; long st4 = 0;
        if (!httpRequest("GET", "https://api.minecraftservices.com/entitlements/mcstore", h4, "", resp4, &st4)) { logErr("\u68C0\u67E5\u6E38\u620F\u6240\u6709\u6743\u5931\u8D25"); return false; }
        Json j4 = jsonParse(resp4);
        bool owned = false;
        if (j4.get("items").isArr()) {
            for (size_t i = 0; i < j4.get("items").size(); i++) {
                if (j4.get("items").at(i).get("name").str() == "game_minecraft") { owned = true; break; }
            }
        }
        if (!owned) { logErr("\u8BE5\u8D26\u53F7\u672A\u8D2D\u4E70 Minecraft(Java \u7248), \u65E0\u6CD5\u767B\u5F55"); return false; }

        // 6. Get player profile
        std::map<std::string, std::string> h5;
        h5["Authorization"] = "Bearer " + mcAccessToken;
        std::string resp5; long st5 = 0;
        if (!httpRequest("GET", "https://api.minecraftservices.com/minecraft/profile", h5, "", resp5, &st5)) { logErr("\u83B7\u53D6\u73A9\u5BB6\u4FE1\u606F\u5931\u8D25"); return false; }
        Json j5 = jsonParse(resp5);
        uuid = j5.get("id").str();
        name = j5.get("name").str();
        if (uuid.empty() || name.empty()) { logErr("\u83B7\u53D6\u73A9\u5BB6\u4FE1\u606F\u5931\u8D25: \u65E0 profile"); return false; }
        return true;
    }
}

static void loginMicrosoftWorker() {
    std::string deviceCode, userCode, verifyUri, accessToken, refreshToken;
    int interval = 5, expiresIn = 900;
    if (!msDeviceCode(deviceCode, userCode, verifyUri, interval, expiresIn)) {
        setStatus("\u5FAE\u8F6F\u767B\u5F55\u5931\u8D25");
        return;
    }
    // Notify UI to show the code
    { std::lock_guard<std::mutex> lk(G->mtx); G->msCode = userCode; G->msVerifyUri = verifyUri; }
    PostMessage(G->hwnd, WM_APP + 10, 0, 0);
    // Wait for user to confirm input
    int waited = 0;
    while (!G->msAck.load() && waited < expiresIn * 1000) { Sleep(100); waited += 100; }

    setStatus("\u7B49\u5F85\u5FAE\u8F6F\u6388\u6743\u4E2D...");
    bool got = false;
    int deadline = expiresIn;
    int waited2 = 0;
    while (waited2 < deadline * 1000) {
        if (msPollToken(deviceCode, accessToken, refreshToken)) { got = true; break; }
        Sleep(interval * 1000);
        waited2 += interval * 1000;
    }
    if (!got) { setStatus("\u6388\u6743\u8D85\u65F6\u6216\u672A\u5B8C\u6210"); logErr("\u5FAE\u8F6F\u767B\u5F55: \u6388\u6743\u8D85\u65F6"); return; }

    std::string mcToken, uuid, name;
    setStatus("\u9A8C\u8BC1 Xbox \u4E0E Minecraft \u8EAB\u4EFD...");
    if (!msXboxChain(accessToken, mcToken, uuid, name)) { setStatus("\u5FAE\u8F6F\u767B\u5F55\u5931\u8D25"); return; }

    G->accountType = "msa";
    G->username = name;
    G->uuid = uuid;
    G->accessToken = mcToken;
    G->refreshToken = refreshToken;
    G->userType = "msa";
    saveConfig();
    logInfo("\u5FAE\u8F6F\u767B\u5F55\u6210\u529F: " + name);
    setStatus("\u767B\u5F55\u6210\u529F: " + name);
    PostMessage(G->hwnd, WM_APP + 11, 0, 0);
}

static void startMicrosoftLogin() {
    G->msAck = false;
    std::thread t(loginMicrosoftWorker);
    t.detach();
}

// ============================================================================
//  10. Version / assets / library download & install
// ============================================================================

static const std::string META_MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
static const std::string RESOURCES_BASE = "https://resources.download.minecraft.net/";
static const std::string LIBRARIES_BASE = "https://libraries.minecraft.net/";

static bool refreshManifest() {
    Json j;
    if (!httpGetJson(META_MANIFEST, j)) { logErr("\u83B7\u53D6\u7248\u672C\u6E05\u5355\u5931\u8D25"); return false; }
    const Json& arr = j.get("versions");
    G->remoteVersions.clear();
    if (arr.isArr()) {
        for (size_t i = 0; i < arr.size(); i++) {
            std::string id = arr.at(i).get("id").str();
            std::string url = arr.at(i).get("url").str();
            if (!id.empty()) G->remoteVersions[id] = url;
        }
    }
    logInfo("\u7248\u672C\u6E05\u5355\u5DF2\u5237\u65B0, \u5171 " + std::to_string(G->remoteVersions.size()) + " \u4E2A\u7248\u672C");
    return true;
}

static bool downloadJsonTo(const std::string& url, const std::string& path, Json& out) {
    std::string body;
    if (!httpGet(url, body)) return false;
    out = jsonParse(body);
    writeFile(path, body);
    return true;
}

struct LibEntry {
    std::string name;
    std::string path;       // Relative to libraries dir
    std::string url;        // Download URL
    std::string sha1;
    std::string nativeClassifier; // If native
    std::string nativeUrl;
};

// Parse library name "group:artifact:version[:classifier]" -> maven path
static std::string mavenPath(const std::string& name) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == ':') { parts.push_back(cur); cur.clear(); }
        else cur += name[i];
    }
    parts.push_back(cur);
    if (parts.size() < 3) return "";
    std::string group = parts[0], artifact = parts[1], version = parts[2];
    std::string classifier = parts.size() > 3 ? parts[3] : "";
    std::string gp = replaceAll(group, ".", "/");
    std::string fname = artifact + "-" + version + (classifier.empty() ? "" : "-" + classifier) + ".jar";
    return gp + "/" + artifact + "/" + version + "/" + fname;
}

// 4th segment classifier of "group:artifact:version[:classifier]" (empty if none)
static std::string nameClassifier(const std::string& name) {
    int colons = 0;
    for (size_t i = 0; i < name.size(); i++) if (name[i] == ':') colons++;
    if (colons < 3) return "";
    size_t pos = name.find_last_of(':');
    return name.substr(pos + 1);
}

static std::string currentOsName() {
    // Treat target platform as windows
    return "windows";
}

static std::string currentOsArch() {
#ifdef _WIN64
    return "x86_64";
#else
    return "x86";
#endif
}

// Check if a rule matches current env (os.name / os.arch / features)
static bool ruleApplies(const Json& r, const std::string& osName, const std::string& osArch) {
    const Json& os = r.get("os");
    if (os.isObj()) {
        std::string n = os.get("name").str();
        if (!n.empty() && n != osName) return false;
        std::string a = os.get("arch").str();
        if (!a.empty() && a != osArch) return false;
    }
    const Json& feats = r.get("features");
    if (feats.isObj() && !feats.o.empty()) {
        // Launcher supports no optional features (demo/custom resolution/quick play), treat as non-match
        return false;
    }
    return true;
}

// Rules: default disallow, match in order, last matching rule wins
static bool rulesAllow(const Json& rules, const std::string& osName, const std::string& osArch) {
    if (!rules.isArr() || rules.size() == 0) return true;
    bool allowed = false;
    for (size_t i = 0; i < rules.size(); i++) {
        const Json& r = rules.at(i);
        if (!ruleApplies(r, osName, osArch)) continue;
        allowed = (r.get("action").str("allow") == "allow");
    }
    return allowed;
}

// Whether a natives classifier matches current platform
static bool nativesMatchesOs(const std::string& classifier, const std::string& osName, const std::string& osArch) {
    if (osName == "windows") {
        if (classifier.find("natives-windows") != 0) return false;
        std::string rest = classifier.substr(std::string("natives-windows").size());
        if (rest.empty()) return osArch == "x86_64";
        if (rest == "-x86" || rest == "-32") return osArch == "x86";
        if (rest == "-64") return osArch == "x86_64";
        if (rest == "-arm64") return osArch == "arm64";
        return false;
    }
    if (osName == "osx") {
        if (classifier.find("natives-macos") == 0) {
            std::string rest = classifier.substr(std::string("natives-macos").size());
            if (rest.empty() || rest == "-patch") return osArch == "x86_64";
            if (rest == "-arm64") return osArch == "arm64";
            return false;
        }
        return classifier.find("natives-osx") == 0;
    }
    if (osName == "linux") return classifier.find("natives-linux") == 0;
    return false;
}

static bool isNativesClassifier(const std::string& c) { return c.find("natives-") == 0; }

// Old natives field: resolve actual classifier (handles -32/-64 cases)
static std::string resolveOldNatives(const Json& dl, const Json& nat, const std::string& osName, const std::string& osArch) {
    std::string base = nat.get(osName).str();
    if (base.empty()) return "";
    // Forge-style placeholder ${arch} -> 32/64
    base = replaceAll(base, "${arch}", osArch == "x86_64" ? "64" : "32");
    const Json& classifiers = dl.get("classifiers");
    if (classifiers.has(base)) return base;
    if (osName == "windows") {
        std::string prefer = base + (osArch == "x86_64" ? "-64" : "-32");
        if (classifiers.has(prefer)) return prefer;
        std::string alt = base + (osArch == "x86_64" ? "-32" : "-64");
        if (classifiers.has(alt)) return alt;
    }
    return base;
}

// Collect libraries from version json (supports 1.13+ new style and old natives field)
static void collectLibraries(const Json& ver, const std::string& osName, const std::string& osArch,
    std::vector<LibEntry>& out, std::vector<LibEntry>& natives) {
    const Json& libs = ver.get("libraries");
    if (!libs.isArr()) return;
    for (size_t i = 0; i < libs.size(); i++) {
        const Json& lib = libs.at(i);
        if (!rulesAllow(lib.get("rules"), osName, osArch)) continue;
        std::string name = lib.get("name").str();
        if (name.empty()) continue;

        const Json& dl = lib.get("downloads");
        std::string classifier = nameClassifier(name);

        // Case 1: modern natives (name carries natives-* classifier, 1.13+)
        if (!classifier.empty() && isNativesClassifier(classifier)) {
            if (!nativesMatchesOs(classifier, osName, osArch)) continue;
            std::string url = dl.get("artifact").get("url").str();
            if (url.empty()) url = LIBRARIES_BASE + mavenPath(name);
            LibEntry e;
            e.name = name; e.path = mavenPath(name); e.url = url;
            e.sha1 = dl.get("artifact").get("sha1").str();
            e.nativeClassifier = classifier;
            natives.push_back(e);
            continue;
        }

        // Case 2: old natives field
        std::string nClass;
        const Json& nat = lib.get("natives");
        if (nat.isObj()) nClass = resolveOldNatives(dl, nat, osName, osArch);
        if (!nClass.empty()) {
            std::string url;
            if (dl.has("classifiers") && dl.get("classifiers").has(nClass))
                url = dl.get("classifiers").get(nClass).get("url").str();
            if (url.empty()) url = LIBRARIES_BASE + mavenPath(name + ":" + nClass);
            LibEntry e;
            e.name = name; e.path = mavenPath(name + ":" + nClass); e.url = url;
            e.nativeClassifier = nClass;
            natives.push_back(e);
            continue;
        }

        // Case 3: regular library
        std::string url = dl.get("artifact").get("url").str();
        if (url.empty()) url = LIBRARIES_BASE + mavenPath(name);
        LibEntry e;
        e.name = name; e.path = mavenPath(name); e.url = url;
        e.sha1 = dl.get("artifact").get("sha1").str();
        out.push_back(e);
    }
}

static bool progressCb_global(long long done, long long total, void* ctx) {
    if (ctx) {
        std::string* label = (std::string*)ctx;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s: %.1f MB / %.1f MB", label->c_str(),
            done / 1048576.0, total > 0 ? total / 1048576.0 : 0);
        setStatus(buf);
        setProgress((int)(done / 1024), (int)(total > 0 ? total / 1024 : 1));
    }
    return true;
}

static bool ensureLib(const std::string& libDir, const LibEntry& e) {
    std::string target = join(libDir, e.path);
    if (fileExists(target) && fileSize(target) > 0) return true;
    logInfo("\u4E0B\u8F7D\u5E93: " + e.name);
    std::string label = "\u4E0B\u8F7D\u5E93 " + e.name;
    fireHook(EVENT_DOWNLOAD_START, e.name.c_str(), NULL, 0);
    bool ok = downloadFile(e.url, target, progressCb_global, &label);
    fireHook(EVENT_DOWNLOAD_DONE, e.name.c_str(), NULL, ok ? 0 : -1);
    if (!ok) logErr("\u4E0B\u8F7D\u5931\u8D25: " + e.url);
    return ok;
}

static bool installVanilla(const std::string& versionId) {
    if (G->remoteVersions.empty()) refreshManifest();
    std::map<std::string, std::string>::const_iterator it = G->remoteVersions.find(versionId);
    if (it == G->remoteVersions.end()) { logErr("\u672A\u77E5\u7248\u672C: " + versionId); return false; }

    std::string verDir = join(G->gameDir, "versions");
    mkdirs(verDir);
    std::string verJsonPath = join(join(verDir, versionId), versionId + ".json");

    Json ver;
    setStatus("\u4E0B\u8F7D\u7248\u672C\u63CF\u8FF0 " + versionId + "...");
    if (!downloadJsonTo(it->second, verJsonPath, ver)) { logErr("\u4E0B\u8F7D\u7248\u672C json \u5931\u8D25"); return false; }

    // Main jar
    const Json& client = ver.get("downloads").get("client");
    std::string clientUrl = client.get("url").str();
    std::string jarPath = join(join(verDir, versionId), versionId + ".jar");
    if (!fileExists(jarPath)) {
        std::string label = "\u4E0B\u8F7D\u4E3B jar";
        if (!downloadFile(clientUrl, jarPath, progressCb_global, &label)) { logErr("\u4E0B\u8F7D\u4E3B jar \u5931\u8D25"); return false; }
    }

    // Libraries
    std::string libDir = join(G->gameDir, "libraries");
    std::vector<LibEntry> libs, natives;
    collectLibraries(ver, currentOsName(), currentOsArch(), libs, natives);
    // Parallel download all libraries (incl. natives jar)
    std::vector<DlTask> libTasks;
    for (size_t i = 0; i < libs.size(); i++) {
        std::string target = join(libDir, libs[i].path);
        if (!fileExists(target) || fileSize(target) <= 0) {
            DlTask t; t.url = libs[i].url; t.target = target; libTasks.push_back(t);
        }
    }
    for (size_t i = 0; i < natives.size(); i++) {
        std::string target = join(libDir, natives[i].path);
        if (!fileExists(target) || fileSize(target) <= 0) {
            DlTask t; t.url = natives[i].url; t.target = target; libTasks.push_back(t);
        }
    }
    int libFailed = parallelDownload(libTasks, 8, "\u4E0B\u8F7D\u5E93");
    if (libFailed > 0) { logErr("\u6709 " + std::to_string(libFailed) + " \u4E2A\u5E93\u4E0B\u8F7D\u5931\u8D25"); return false; }

    // Extract natives
    std::string nativesDir = join(join(verDir, versionId), "natives");
    mkdirs(nativesDir);
    for (size_t i = 0; i < natives.size(); i++) {
        extractZipAll(join(libDir, natives[i].path), nativesDir);
    }

    // Asset index
    const Json& ai = ver.get("assetIndex");
    std::string aiUrl = ai.get("url").str();
    std::string aiId = ai.get("id").str();
    std::string indexesDir = join(G->gameDir, "assets\\indexes");
    std::string aiPath = join(indexesDir, aiId + ".json");
    Json aiJson;
    if (!fileExists(aiPath)) {
        if (!downloadJsonTo(aiUrl, aiPath, aiJson)) { logErr("\u4E0B\u8F7D\u8D44\u6E90\u7D22\u5F15\u5931\u8D25"); return false; }
    } else {
        std::string t; readFile(aiPath, t); aiJson = jsonParse(t);
    }

    // Full objects download (parallel speedup)
    const Json& objects = aiJson.get("objects");
    std::string objBase = join(G->gameDir, "assets\\objects");
    if (objects.isObj()) {
        std::vector<DlTask> tasks;
        for (std::map<std::string, Json>::const_iterator oi = objects.o.begin(); oi != objects.o.end(); ++oi) {
            std::string hash = oi->second.get("hash").str();
            if (hash.size() < 2) continue;
            std::string rel = hash.substr(0, 2) + "/" + hash;
            std::string target = join(objBase, rel);
            if (!fileExists(target) || fileSize(target) <= 0) {
                DlTask t;
                t.url = RESOURCES_BASE + rel;
                t.target = target;
                tasks.push_back(t);
            }
        }
        logInfo("\u8D44\u6E90 objects: \u5171 " + std::to_string(objects.o.size()) + " \u4E2A, \u9700\u4E0B\u8F7D " + std::to_string(tasks.size()) + " \u4E2A");
        int failed = parallelDownload(tasks, 16, "\u4E0B\u8F7D\u8D44\u6E90");
        if (failed > 0) logErr("\u6709 " + std::to_string(failed) + " \u4E2A\u8D44\u6E90\u4E0B\u8F7D\u5931\u8D25");
    }

    // log4j config file (required 1.7+, missing causes error on 1.17+)
    const Json& logfile = ver.get("logging").get("client").get("file");
    std::string logId = logfile.get("id").str();
    std::string logUrl = logfile.get("url").str();
    if (!logId.empty() && !logUrl.empty()) {
        std::string logPath = join(G->gameDir, "assets\\log_configs\\" + logId);
        if (!fileExists(logPath)) {
            std::string label = "\u4E0B\u8F7D log4j \u914D\u7F6E";
            downloadFile(logUrl, logPath, progressCb_global, &label);
        }
    }

    logInfo("\u539F\u7248 " + versionId + " \u5B89\u88C5\u5B8C\u6210");
    return true;
}

// ---- Forge / NeoForge (installer jar method, with processor execution) ----
static std::string findJava();

// Read Main-Class from MANIFEST.MF inside a jar
static std::string readJarMainClass(const std::string& jarPath) {
    std::vector<unsigned char> mf;
    if (!readZipFile(jarPath, "META-INF/MANIFEST.MF", mf)) return "";
    std::string s(mf.begin(), mf.end());
    size_t pos = s.find("Main-Class:");
    if (pos == std::string::npos) return "";
    size_t e = s.find_first_of("\r\n", pos);
    return trimStr(s.substr(pos + 11, e == std::string::npos ? std::string::npos : e - pos - 11));
}

static std::string libPathOf(const std::string& name, const std::string& libDir) {
    return join(libDir, mavenPath(name));
}

// Forge processor argument token substitution
static std::string forgeExpand(const std::string& arg,
    const std::map<std::string, std::string>& data, const std::string& libDir,
    const std::string& mcJar, const std::string& installer) {
    std::string out;
    for (size_t i = 0; i < arg.size(); i++) {
        if (arg[i] == '{') {
            size_t e = arg.find('}', i + 1);
            if (e != std::string::npos) {
                std::string key = arg.substr(i + 1, e - i - 1);
                if (key == "MINECRAFT_JAR") out += mcJar;
                else if (key == "SIDE") out += "client";
                else if (key == "ROOT") out += G->gameDir;
                else if (key == "INSTALLER") out += installer;
                else {
                    std::map<std::string, std::string>::const_iterator it = data.find(key);
                    if (it != data.end()) out += it->second;
                    else out += "{" + key + "}";
                }
                i = e;
                continue;
            }
        }
        if (arg[i] == '[') {
            size_t e = arg.find(']', i + 1);
            if (e != std::string::npos) {
                out += libPathOf(arg.substr(i + 1, e - i - 1), libDir);
                i = e;
                continue;
            }
        }
        out += arg[i];
    }
    return out;
}

// Run Forge install processors (patch jar / generate files)
static bool runForgeProcessors(const Json& ip, const std::string& libDir,
    const std::string& mcJar, const std::string& installer) {
    const Json& procs = ip.get("processors");
    if (!procs.isArr() || procs.size() == 0) return true;
    std::string java = findJava();
    if (java.empty()) { logErr("Forge \u5B89\u88C5\u9700\u8981 Java \u6267\u884C\u5904\u7406\u5668, \u672A\u627E\u5230 Java"); return false; }

    std::map<std::string, std::string> data;
    const Json& d = ip.get("data");
    if (d.isObj()) {
        for (std::map<std::string, Json>::const_iterator it = d.o.begin(); it != d.o.end(); ++it) {
            std::string v = it->second.get("client").str();
            if (!v.empty()) data[it->first] = libPathOf(v, libDir);
        }
    }
    for (size_t p = 0; p < procs.size(); p++) {
        const Json& proc = procs.at(p);
        if (proc.has("sides") && proc.get("sides").isArr()) {
            bool hasClient = false;
            for (size_t s = 0; s < proc.get("sides").size(); s++)
                if (proc.get("sides").at(s).str() == "client") { hasClient = true; break; }
            if (!hasClient) continue;
        }
        std::string mainName = proc.get("jar").str();
        std::string mainJar = libPathOf(mainName, libDir);
        if (!fileExists(mainJar)) { logErr("\u5904\u7406\u5668\u7F3A\u5931: " + mainName); continue; }
        std::string mainClass = readJarMainClass(mainJar);
        if (mainClass.empty()) { logErr("\u5904\u7406\u5668\u65E0 Main-Class: " + mainName); continue; }

        std::vector<std::string> cps;
        const Json& cpath = proc.get("classpath");
        if (cpath.isArr()) {
            for (size_t c = 0; c < cpath.size(); c++) {
                std::string lp = libPathOf(cpath.at(c).str(), libDir);
                if (fileExists(lp)) cps.push_back(lp);
            }
        }
        if (std::find(cps.begin(), cps.end(), mainJar) == cps.end()) cps.push_back(mainJar);
        std::string cp;
        for (size_t c = 0; c < cps.size(); c++) { if (c) cp += ";"; cp += cps[c]; }

        std::vector<std::string> args;
        const Json& a = proc.get("args");
        if (a.isArr()) {
            for (size_t k = 0; k < a.size(); k++)
                args.push_back(forgeExpand(a.at(k).str(), data, libDir, mcJar, installer));
        }
        std::string cmd = "\"" + java + "\" -cp \"" + cp + "\" \"" + mainClass + "\"";
        for (size_t k = 0; k < args.size(); k++) cmd += " \"" + args[k] + "\"";
        logInfo("\u6267\u884C Forge \u5904\u7406\u5668: " + mainClass);

        STARTUPINFOA si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
        PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
        std::vector<char> cbuf(cmd.begin(), cmd.end()); cbuf.push_back('\0');
        if (!CreateProcessA(NULL, &cbuf[0], NULL, NULL, FALSE, 0, NULL, G->gameDir.c_str(), &si, &pi)) {
            logErr("\u5904\u7406\u5668\u542F\u52A8\u5931\u8D25: " + mainClass);
            continue;
        }
        WaitForSingleObject(pi.hProcess, 180000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    return true;
}

static bool installForgeLike(const std::string& mcVersion, const std::string& loaderVersion,
    const std::string& baseUrl, const std::string& label) {
    std::string mc = mcVersion, lv = loaderVersion;
    std::string installerUrl = baseUrl + "/net/minecraftforge/forge/" + mc + "-" + lv + "/forge-" + mc + "-" + lv + "-installer.jar";
    std::string installerPath = join(G->launcherDir, "forge-" + mc + "-" + lv + "-installer.jar");
    logInfo("\u4E0B\u8F7D " + label + " \u5B89\u88C5\u5668: " + installerUrl);
    std::string dl = "\u4E0B\u8F7D\u5B89\u88C5\u5668";
    if (!fileExists(installerPath)) {
        if (!downloadFile(installerUrl, installerPath, progressCb_global, &dl)) {
            logErr("\u4E0B\u8F7D\u5B89\u88C5\u5668\u5931\u8D25, \u8BF7\u786E\u8BA4\u7248\u672C\u7EC4\u5408 " + mc + "-" + lv + " \u5B58\u5728");
            return false;
        }
    }
    std::vector<unsigned char> ipBytes, vjBytes;
    if (!readZipFile(installerPath, "install_profile.json", ipBytes)) { logErr("\u5B89\u88C5\u5668\u7F3A\u5C11 install_profile.json"); return false; }
    Json ip = jsonParse(std::string(ipBytes.begin(), ipBytes.end()));

    std::string versionJsonName = "version.json";
    if (!readZipFile(installerPath, versionJsonName, vjBytes)) {
        versionJsonName = "data/version.json";
        if (!readZipFile(installerPath, versionJsonName, vjBytes)) { logErr("\u5B89\u88C5\u5668\u7F3A\u5C11 version.json"); return false; }
    }
    Json vj = jsonParse(std::string(vjBytes.begin(), vjBytes.end()));

    std::string forgeId = ip.get("version").str();
    if (forgeId.empty()) forgeId = vj.get("id").str();
    if (forgeId.empty()) forgeId = mc + "-" + label + "-" + lv;

    std::string libDir = join(G->gameDir, "libraries");
    const Json& fLibs = ip.get("libraries");
    if (fLibs.isArr()) {
        for (size_t i = 0; i < fLibs.size(); i++) {
            const Json& lib = fLibs.at(i);
            std::string name = lib.get("name").str();
            if (name.empty()) continue;
            std::string url = lib.get("downloads").get("artifact").get("url").str();
            if (url.empty()) {
                std::string root = lib.get("url").str();
                if (!root.empty()) {
                    if (root[root.size() - 1] != '/') root += "/";
                    url = root + mavenPath(name);
                } else {
                    url = "https://maven.minecraftforge.net/" + mavenPath(name);
                }
            }
            LibEntry e;
            e.name = name; e.path = mavenPath(name); e.url = url;
            ensureLib(libDir, e);
        }
    }
    if (!installVanilla(mc)) { logErr("\u539F\u7248 " + mc + " \u5B89\u88C5\u5931\u8D25, " + label + " \u4F9D\u8D56\u5B83"); }

    std::string mcJar = join(join(G->gameDir, "versions\\" + mc), mc + ".jar");
    runForgeProcessors(ip, libDir, mcJar, installerPath);

    std::string vdir = join(G->gameDir, "versions\\" + forgeId);
    mkdirs(vdir);
    writeFile(join(vdir, forgeId + ".json"), jsonStringify(vj));
    logInfo(label + " " + forgeId + " \u5B89\u88C5\u5B8C\u6210");
    return true;
}

// Get latest loader version for an MC version from maven-metadata.xml
static bool latestLoaderVersion(const std::string& mcVersion, const std::string& metadataUrl, std::string& out) {
    std::string body;
    if (!httpGet(metadataUrl, body)) return false;
    std::vector<std::string> vers;
    size_t pos = 0;
    while ((pos = body.find("<version>", pos)) != std::string::npos) {
        size_t s = pos + 9;
        size_t e = body.find("</version>", s);
        if (e == std::string::npos) break;
        std::string v = body.substr(s, e - s);
        if (v.find(mcVersion + "-") == 0) vers.push_back(v);
        pos = e + 10;
    }
    if (vers.empty()) return false;
    out = vers[vers.size() - 1];
    return true;
}

static bool installForge(const std::string& mcVersion) {
    std::string ver;
    if (!latestLoaderVersion(mcVersion, "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml", ver)) {
        logErr("\u672A\u627E\u5230 " + mcVersion + " \u5BF9\u5E94\u7684 Forge \u7248\u672C");
        return false;
    }
    std::string build = ver.substr(mcVersion.size() + 1);
    return installForgeLike(mcVersion, build, "https://maven.minecraftforge.net", "Forge");
}

static bool installNeoForge(const std::string& mcVersion) {
    std::string ver;
    if (!latestLoaderVersion(mcVersion, "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml", ver)) {
        logErr("\u672A\u627E\u5230 " + mcVersion + " \u5BF9\u5E94\u7684 NeoForge \u7248\u672C");
        return false;
    }
    std::string installerUrl = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + ver + "/neoforge-" + ver + "-installer.jar";
    std::string installerPath = join(G->launcherDir, "neoforge-" + ver + "-installer.jar");
    logInfo("\u4E0B\u8F7D NeoForge \u5B89\u88C5\u5668: " + installerUrl);
    std::string dl = "\u4E0B\u8F7D\u5B89\u88C5\u5668";
    if (!fileExists(installerPath)) {
        if (!downloadFile(installerUrl, installerPath, progressCb_global, &dl)) { logErr("\u4E0B\u8F7D NeoForge \u5B89\u88C5\u5668\u5931\u8D25"); return false; }
    }
    std::vector<unsigned char> ipBytes, vjBytes;
    if (!readZipFile(installerPath, "install_profile.json", ipBytes)) { logErr("\u5B89\u88C5\u5668\u7F3A\u5C11 install_profile.json"); return false; }
    Json ip = jsonParse(std::string(ipBytes.begin(), ipBytes.end()));
    if (!readZipFile(installerPath, "version.json", vjBytes)) { logErr("\u5B89\u88C5\u5668\u7F3A\u5C11 version.json"); return false; }
    Json vj = jsonParse(std::string(vjBytes.begin(), vjBytes.end()));
    std::string forgeId = ip.get("version").str();
    if (forgeId.empty()) forgeId = vj.get("id").str();
    if (forgeId.empty()) forgeId = "neoforge-" + ver;

    std::string libDir = join(G->gameDir, "libraries");
    const Json& fLibs = ip.get("libraries");
    if (fLibs.isArr()) {
        for (size_t i = 0; i < fLibs.size(); i++) {
            const Json& lib = fLibs.at(i);
            std::string name = lib.get("name").str();
            if (name.empty()) continue;
            std::string url = lib.get("downloads").get("artifact").get("url").str();
            if (url.empty()) {
                std::string root = lib.get("url").str();
                if (!root.empty()) { if (root[root.size() - 1] != '/') root += "/"; url = root + mavenPath(name); }
                else url = "https://maven.neoforged.net/releases/" + mavenPath(name);
            }
            LibEntry e; e.name = name; e.path = mavenPath(name); e.url = url;
            ensureLib(libDir, e);
        }
    }
    installVanilla(mcVersion);
    std::string mcJar = join(join(G->gameDir, "versions\\" + mcVersion), mcVersion + ".jar");
    runForgeProcessors(ip, libDir, mcJar, installerPath);
    std::string vdir = join(G->gameDir, "versions\\" + forgeId);
    mkdirs(vdir);
    writeFile(join(vdir, forgeId + ".json"), jsonStringify(vj));
    logInfo("NeoForge " + forgeId + " \u5B89\u88C5\u5B8C\u6210");
    return true;
}

// ---- Fabric ----
static bool installFabric(const std::string& mcVersion) {
    // 1. Get latest loader version
    Json loaderList;
    if (!httpGetJson("https://meta.fabricmc.net/v2/versions/loader/" + mcVersion, loaderList)) {
        logErr("\u83B7\u53D6 Fabric loader \u5217\u8868\u5931\u8D25"); return false;
    }
    if (!loaderList.isArr() || loaderList.size() == 0) { logErr("\u8BE5\u7248\u672C\u65E0\u53EF\u7528 Fabric loader"); return false; }
    std::string loaderVer = loaderList.at(0).get("loader").get("version").str();
    if (loaderVer.empty()) { logErr("\u89E3\u6790 Fabric loader \u7248\u672C\u5931\u8D25"); return false; }

    // 2. Get profile json
    Json prof;
    if (!httpGetJson("https://meta.fabricmc.net/v2/versions/loader/" + mcVersion + "/" + loaderVer + "/profile/json", prof)) {
        logErr("\u83B7\u53D6 Fabric profile \u5931\u8D25"); return false;
    }
    std::string id = "fabric-loader-" + loaderVer + "-" + mcVersion;
    prof.o["id"] = prof.o["id"].isNull() ? Json() : prof.o["id"];
    Json pid; pid.t = Json::STR; pid.s = id; prof.o["id"] = pid;
    // Ensure inherits vanilla
    if (!prof.has("inheritsFrom")) { Json in; in.t = Json::STR; in.s = mcVersion; prof.o["inheritsFrom"] = in; }

    // 3. Vanilla
    if (!installVanilla(mcVersion)) { logErr("\u539F\u7248\u5B89\u88C5\u5931\u8D25, Fabric \u4F9D\u8D56\u5B83"); }

    // 4. Download fabric libraries
    std::string libDir = join(G->gameDir, "libraries");
    std::vector<LibEntry> libs, natives;
    collectLibraries(prof, currentOsName(), currentOsArch(), libs, natives);
    for (size_t i = 0; i < libs.size(); i++) {
        // Fabric libs live on maven.fabricmc.net; collectLibraries may have filled
        // a default libraries.minecraft.net URL, which 404s for fabric artifacts.
        if (libs[i].url.empty() || libs[i].url.find("libraries.minecraft.net") != std::string::npos)
            libs[i].url = "https://maven.fabricmc.net/" + libs[i].path;
        ensureLib(libDir, libs[i]);
    }
    for (size_t i = 0; i < natives.size(); i++) {
        if (natives[i].url.empty() || natives[i].url.find("libraries.minecraft.net") != std::string::npos)
            natives[i].url = "https://maven.fabricmc.net/" + natives[i].path;
        ensureLib(libDir, natives[i]);
    }

    // 5. Write version json
    std::string vdir = join(G->gameDir, "versions\\" + id);
    mkdirs(vdir);
    writeFile(join(vdir, id + ".json"), jsonStringify(prof));
    logInfo("Fabric " + id + " \u5B89\u88C5\u5B8C\u6210");
    return true;
}

// ---- Quilt ----
static bool installQuilt(const std::string& mcVersion) {
    Json loaderList;
    if (!httpGetJson("https://meta.quiltmc.org/v3/versions/loader/" + mcVersion, loaderList)) {
        logErr("\u83B7\u53D6 Quilt loader \u5217\u8868\u5931\u8D25"); return false;
    }
    if (!loaderList.isArr() || loaderList.size() == 0) { logErr("\u8BE5\u7248\u672C\u65E0 Quilt loader"); return false; }
    std::string loaderVer = loaderList.at(0).get("loader").get("version").str();
    Json prof;
    if (!httpGetJson("https://meta.quiltmc.org/v3/versions/loader/" + mcVersion + "/" + loaderVer + "/profile/json", prof)) {
        logErr("\u83B7\u53D6 Quilt profile \u5931\u8D25"); return false;
    }
    std::string id = "quilt-loader-" + loaderVer + "-" + mcVersion;
    Json pid; pid.t = Json::STR; pid.s = id; prof.o["id"] = pid;
    if (!prof.has("inheritsFrom")) { Json in; in.t = Json::STR; in.s = mcVersion; prof.o["inheritsFrom"] = in; }
    installVanilla(mcVersion);
    std::string libDir = join(G->gameDir, "libraries");
    std::vector<LibEntry> libs, natives;
    collectLibraries(prof, currentOsName(), currentOsArch(), libs, natives);
    for (size_t i = 0; i < libs.size(); i++) { if (libs[i].url.empty()) libs[i].url = "https://maven.quiltmc.org/repository/release/" + libs[i].path; ensureLib(libDir, libs[i]); }
    for (size_t i = 0; i < natives.size(); i++) { if (natives[i].url.empty()) natives[i].url = "https://maven.quiltmc.org/repository/release/" + natives[i].path; ensureLib(libDir, natives[i]); }
    std::string vdir = join(G->gameDir, "versions\\" + id);
    mkdirs(vdir);
    writeFile(join(vdir, id + ".json"), jsonStringify(prof));
    logInfo("Quilt " + id + " \u5B89\u88C5\u5B8C\u6210");
    return true;
}

// ============================================================================
//  11. Launch game
// ============================================================================

static std::string findJavaUnder(const std::string& dir, int depth) {
    if (depth > 8) return "";
    WIN32_FIND_DATAW fd;
    std::wstring pattern = utf8tow(join(dir, "*"));
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string found;
    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        std::string full = join(dir, wtoutf8(name));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                found = findJavaUnder(full, depth + 1);
                if (!found.empty()) break;
            }
        } else {
            std::string lower = lowerStr(wtoutf8(name));
            if (lower == "java.exe" || lower == "javaw.exe") { found = full; break; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static std::string findJava() {
    // 1. Path from config
    if (!G->javaPath.empty() && fileExists(G->javaPath)) return G->javaPath;
    // 2. Official launcher bundled runtime .minecraft\runtime\...\bin\java.exe
    std::string rt = findJavaUnder(join(G->gameDir, "runtime"), 0);
    if (!rt.empty()) return rt;
    // 3. Search java.exe in PATH
    char* path = getenv("PATH");
    if (path) {
        std::string p = path;
        size_t start = 0;
        while (start <= p.size()) {
            size_t semi = p.find(';', start);
            std::string dir = p.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            if (!dir.empty()) {
                std::string cand = join(dir, "java.exe");
                if (fileExists(cand)) return cand;
            }
            if (semi == std::string::npos) break;
            start = semi + 1;
        }
    }
    // 4. Common JDK paths (recursive)
    const char* commons[] = {
        "C:\\Program Files\\Java", "C:\\Program Files (x86)\\Java", "C:\\Program Files\\Eclipse Adoptium", "C:\\Program Files\\Microsoft"
    };
    for (int i = 0; i < 4; i++) {
        std::string base = commons[i];
        if (!dirExists(base)) continue;
        std::string cand = findJavaUnder(base, 0);
        if (!cand.empty()) return cand;
    }
    return "";
}

// Runtime platform key (windows-x64 / windows-x86)
static std::string runtimePlatformKey() {
#ifdef _WIN64
    return "windows-x64";
#else
    return "windows-x86";
#endif
}

// Download/fetch official bundled Java runtime by javaVersion.component
static const std::string JRE_MANIFEST_URL = "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";

static std::string ensureJavaRuntime(const std::string& component) {
    if (component.empty()) return "";
    std::string base = join(G->gameDir, "runtime\\" + component);
    std::string javaExe = join(base, "bin\\java.exe");
    if (fileExists(javaExe)) return javaExe;

    logInfo("\u672A\u627E\u5230\u5339\u914D\u7684 Java \u8FD0\u884C\u65F6, \u5F00\u59CB\u4E0B\u8F7D " + component + " ...");
    std::string plat = runtimePlatformKey();
    Json all;
    if (!httpGetJson(JRE_MANIFEST_URL, all)) { logErr("\u83B7\u53D6 Java \u8FD0\u884C\u65F6\u6E05\u5355\u5931\u8D25"); return ""; }
    const Json& platNode = all.get(plat);
    if (!platNode.isObj()) { logErr("\u6E05\u5355\u4E2D\u65E0\u5E73\u53F0 " + plat); return ""; }
    const Json& arr = platNode.get(component);
    if (!arr.isArr() || arr.size() == 0) { logErr("\u6E05\u5355\u4E2D\u65E0\u8FD0\u884C\u65F6\u7EC4\u4EF6 " + component); return ""; }
    std::string manifestUrl = arr.at(0).get("manifest").get("url").str();
    if (manifestUrl.empty()) { logErr("\u65E0\u8FD0\u884C\u65F6 manifest \u5730\u5740"); return ""; }

    Json mf;
    if (!httpGetJson(manifestUrl, mf)) { logErr("\u4E0B\u8F7D Java \u8FD0\u884C\u65F6 manifest \u5931\u8D25"); return ""; }
    const Json& files = mf.get("files");
    int total = files.isObj() ? (int)files.o.size() : 0;
    setStatus("\u4E0B\u8F7D Java \u8FD0\u884C\u65F6 " + component + " (\u5171 " + std::to_string(total) + " \u4E2A\u6587\u4EF6)...");
    if (files.isObj()) {
        std::vector<DlTask> tasks;
        for (std::map<std::string, Json>::const_iterator it = files.o.begin(); it != files.o.end(); ++it) {
            const Json& raw = it->second.get("downloads").get("raw");
            std::string url = raw.get("url").str();
            std::string rel = it->first;
            if (!url.empty() && !rel.empty()) {
                std::string target = join(base, rel);
                if (!fileExists(target)) {
                    DlTask t; t.url = url; t.target = target; tasks.push_back(t);
                }
            }
        }
        int failed = parallelDownload(tasks, 8, "\u4E0B\u8F7D\u8FD0\u884C\u65F6 " + component);
        if (failed > 0) logErr("\u6709 " + std::to_string(failed) + " \u4E2A\u8FD0\u884C\u65F6\u6587\u4EF6\u4E0B\u8F7D\u5931\u8D25");
    }
    if (fileExists(javaExe)) { logInfo("Java \u8FD0\u884C\u65F6 " + component + " \u5B89\u88C5\u5B8C\u6210"); return javaExe; }
    logErr("Java \u8FD0\u884C\u65F6 " + component + " \u5B89\u88C5\u5931\u8D25");
    return "";
}

static std::string expandVars(const std::string& s, const std::map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
            size_t e = s.find('}', i + 2);
            if (e != std::string::npos) {
                std::string key = s.substr(i + 2, e - i - 2);
                std::map<std::string, std::string>::const_iterator it = vars.find(key);
                if (it != vars.end()) out += it->second;
                else out += "${" + key + "}";
                i = e;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

static void collectArgStrings(const Json& arr, const std::string& osName, const std::string& osArch,
    const std::map<std::string, std::string>& vars, std::vector<std::string>& out) {
    if (!arr.isArr()) return;
    for (size_t i = 0; i < arr.size(); i++) {
        const Json& e = arr.at(i);
        if (e.isStr()) { out.push_back(expandVars(e.s, vars)); }
        else if (e.isObj()) {
            if (!rulesAllow(e.get("rules"), osName, osArch)) continue;
            const Json& v = e.get("value");
            if (v.isStr()) out.push_back(expandVars(v.s, vars));
            else if (v.isArr()) {
                for (size_t k = 0; k < v.size(); k++)
                    if (v.at(k).isStr()) out.push_back(expandVars(v.at(k).s, vars));
            }
        }
    }
}

// Return the bottom-most (vanilla) version id in the inheritance chain.
static std::string getBaseVersionId(const std::string& id) {
    std::string cur = id;
    int guard = 0;
    while (!cur.empty() && guard++ < 8) {
        std::string p = join(join(G->gameDir, "versions\\" + cur), cur + ".json");
        std::string txt;
        if (!readFile(p, txt)) break;
        Json v = jsonParse(txt);
        std::string next = v.get("inheritsFrom").str();
        if (next.empty()) break;
        cur = next;
    }
    return cur;
}

// Load version and merge inheritance chain
static bool loadMergedVersion(const std::string& id, Json& out) {
    std::vector<Json> chain;
    std::string cur = id;
    int guard = 0;
    while (!cur.empty() && guard++ < 8) {
        std::string p = join(join(G->gameDir, "versions\\" + cur), cur + ".json");
        std::string txt;
        if (!readFile(p, txt)) return false;
        Json v = jsonParse(txt);
        chain.push_back(v);
        cur = v.get("inheritsFrom").str();
    }
    // Merge from back to front: base first
    Json merged; merged.t = Json::OBJ;
    Json libs; libs.t = Json::ARR;
    std::string mainClass, assetIndexId, assetIndexUrl;
    Json mergedJvm; mergedJvm.t = Json::ARR;
    Json mergedGame; mergedGame.t = Json::ARR;
    bool hasArgs = false;
    std::string minecraftArgs;
    std::string javaVerStr;
    std::string javaComponent;
    Json logging;

    for (size_t i = chain.size(); i-- > 0;) {
        const Json& v = chain[i];
        if (v.get("libraries").isArr())
            for (size_t k = 0; k < v.get("libraries").size(); k++)
                libs.a.push_back(v.get("libraries").at(k));
        // mainClass: the most specific (top of inheritance chain) wins.
        // Forge/Fabric/Quilt override the vanilla main class; keep their value.
        {
            std::string mc = v.get("mainClass").str();
            if (!mc.empty()) mainClass = mc;
        }
        if (assetIndexId.empty()) { assetIndexId = v.get("assetIndex").get("id").str(); assetIndexUrl = v.get("assetIndex").get("url").str(); }
        // Merge arguments.jvm and arguments.game across the chain (deep merge):
        // vanilla provides -cp / game args, loaders append their own JVM flags.
        if (v.has("arguments") && v.get("arguments").isObj()) {
            hasArgs = true;
            const Json& a = v.get("arguments");
            const Json& jvm = a.get("jvm");
            if (jvm.isArr())
                for (size_t k = 0; k < jvm.size(); k++) mergedJvm.a.push_back(jvm.at(k));
            const Json& game = a.get("game");
            if (game.isArr())
                for (size_t k = 0; k < game.size(); k++) mergedGame.a.push_back(game.at(k));
        }
        if (minecraftArgs.empty()) minecraftArgs = v.get("minecraftArguments").str();
        if (javaVerStr.empty()) javaVerStr = v.get("javaVersion").get("majorVersion").str();
        if (javaComponent.empty()) javaComponent = v.get("javaVersion").get("component").str();
        if (logging.isNull() && v.has("logging")) logging = v.get("logging");
    }
    Json m; m.t = Json::STR; m.s = mainClass; merged.o["mainClass"] = m;
    Json ai; ai.t = Json::OBJ;
    Json aid; aid.t = Json::STR; aid.s = assetIndexId; ai.o["id"] = aid;
    Json aiu; aiu.t = Json::STR; aiu.s = assetIndexUrl; ai.o["url"] = aiu;
    merged.o["assetIndex"] = ai;
    merged.o["libraries"] = libs;
    if (hasArgs) {
        Json margs; margs.t = Json::OBJ;
        margs.o["jvm"] = mergedJvm;
        margs.o["game"] = mergedGame;
        merged.o["arguments"] = margs;
    }
    if (!minecraftArgs.empty()) { Json ma; ma.t = Json::STR; ma.s = minecraftArgs; merged.o["minecraftArguments"] = ma; }
    if (!javaVerStr.empty()) { Json jv; jv.t = Json::NUM; jv.n = atof(javaVerStr.c_str()); merged.o["javaMajor"] = jv; }
    if (!javaComponent.empty()) { Json jc; jc.t = Json::STR; jc.s = javaComponent; merged.o["javaComponent"] = jc; }
    if (!logging.isNull()) merged.o["logging"] = logging;
    out = merged;
    return true;
}

static void launchGame(const std::string& versionId) {
    if (G->username.empty()) { logErr("\u8BF7\u5148\u767B\u5F55"); MessageBoxW(G->hwnd, L"\u8BF7\u5148\u767B\u5F55\u8D26\u53F7", L"\u63D0\u793A", MB_ICONINFORMATION); return; }

    Json merged;
    if (!loadMergedVersion(versionId, merged)) { logErr("\u8BFB\u53D6\u7248\u672C\u5931\u8D25: " + versionId); return; }

    // Choose Java by declared javaVersion.component (auto-download if needed)
    std::string java;
    std::string component = merged.get("javaComponent").str();
    if (component.empty()) component = "jre-legacy"; // Pre-1.17 has no such field, default Java 8
    java = ensureJavaRuntime(component);
    if (java.empty()) java = findJava();
    if (java.empty()) {
        logErr("\u672A\u627E\u5230 Java, \u8BF7\u5728\u8BBE\u7F6E\u4E2D\u6307\u5B9A java.exe \u8DEF\u5F84");
        MessageBoxW(G->hwnd, L"\u672A\u627E\u5230 Java \u8FD0\u884C\u65F6\u3002\n\u8BF7\u5728\u8BBE\u7F6E\u4E2D\u6307\u5B9A java.exe \u7684\u5B8C\u6574\u8DEF\u5F84, \u6216\u8054\u7F51\u8BA9\u542F\u52A8\u5668\u81EA\u52A8\u4E0B\u8F7D\u5BF9\u5E94\u7248\u672C\u3002", L"\u542F\u52A8\u5931\u8D25", MB_ICONWARNING);
        return;
    }

    std::string mainClass = merged.get("mainClass").str();
    std::string assetIndexId = merged.get("assetIndex").get("id").str();

    // Version isolation: separate game dir (saves/Mods/config), libs & assets still shared
    std::string instanceDir = G->gameDir;
    if (G->versionIsolation) {
        instanceDir = join(G->launcherDir, "instances\\" + versionId);
        mkdirs(instanceDir);
    }

    // Classpath (use the base vanilla version's jar for inherited loaders like Fabric/Forge)
    std::string baseVersionId = getBaseVersionId(versionId);
    std::vector<std::string> classpath;
    classpath.push_back(join(join(G->gameDir, "versions\\" + baseVersionId), baseVersionId + ".jar"));
    std::string libDir = join(G->gameDir, "libraries");
    std::vector<LibEntry> lentries, natives;
    collectLibraries(merged, currentOsName(), currentOsArch(), lentries, natives);
    for (size_t i = 0; i < lentries.size(); i++) classpath.push_back(join(libDir, lentries[i].path));
    std::string cp;
    for (size_t i = 0; i < classpath.size(); i++) { if (i) cp += ";"; cp += classpath[i]; }

    // natives directory (use the base vanilla version's natives for inherited loaders)
    std::string nativesDir = join(join(G->gameDir, "versions\\" + getBaseVersionId(versionId)), "natives");

    // Variables
    std::map<std::string, std::string> vars;
    vars["auth_player_name"] = G->username;
    vars["version_name"] = versionId;
    vars["game_directory"] = instanceDir;
    vars["assets_root"] = join(G->gameDir, "assets");
    vars["assets_index_name"] = assetIndexId;
    vars["auth_uuid"] = G->uuid.empty() ? offlineUuid(G->username) : G->uuid;
    vars["auth_access_token"] = G->accessToken.empty() ? "0" : G->accessToken;
    vars["auth_session"] = "token:" + vars["auth_access_token"] + ":" + vars["auth_uuid"];
    vars["user_type"] = (G->accountType == "msa") ? "msa" : "legacy";
    vars["version_type"] = "release";
    vars["game_assets"] = vars["assets_root"];
    vars["user_properties"] = "{}";
    vars["clientid"] = "0";
    vars["auth_xuid"] = "0";
    vars["natives_directory"] = nativesDir;
    vars["launcher_name"] = "GDML";
    vars["launcher_version"] = "1.0";
    vars["classpath"] = cp;
    vars["classpath_separator"] = ";";
    vars["library_directory"] = libDir;
    vars["resolution_width"] = "854";
    vars["resolution_height"] = "480";

    // Build arguments
    std::vector<std::string> jvmArgs, gameArgs;
    char memBuf[32];
    snprintf(memBuf, sizeof(memBuf), "-Xmx%dM", G->maxMemoryMB);
    jvmArgs.push_back(memBuf);
    jvmArgs.push_back("-XX:+UseG1GC");

    // log4j config: ensure downloaded and add JVM arg
    const Json& logclient = merged.get("logging").get("client");
    std::string logId = logclient.get("file").get("id").str();
    if (!logId.empty()) {
        std::string logPath = join(G->gameDir, "assets\\log_configs\\" + logId);
        if (!fileExists(logPath)) {
            std::string lu = logclient.get("file").get("url").str();
            if (!lu.empty()) { std::string label = "\u4E0B\u8F7D log4j \u914D\u7F6E"; downloadFile(lu, logPath, progressCb_global, &label); }
        }
        std::string larg = logclient.get("argument").str();
        if (!larg.empty()) jvmArgs.push_back(replaceAll(larg, "${path}", logPath));
    }

    const Json& args = merged.get("arguments");
    if (args.isObj()) {
        // Modern (1.13+): JVM/game args come from arguments (includes -cp, -Djava.library.path, brand, version)
        collectArgStrings(args.get("jvm"), currentOsName(), currentOsArch(), vars, jvmArgs);
        collectArgStrings(args.get("game"), currentOsName(), currentOsArch(), vars, gameArgs);
    } else {
        // Legacy (1.12-): manually add the args that modern versions declare in arguments
        jvmArgs.push_back("-Djava.library.path=" + nativesDir);
        jvmArgs.push_back("-Dminecraft.launcher.brand=GDML");
        jvmArgs.push_back("-Dminecraft.launcher.version=1.0");
        jvmArgs.push_back("-cp");
        jvmArgs.push_back(cp);
    }
    if (gameArgs.empty()) {
        // Legacy format (1.12-): expand minecraftArguments directly
        std::string legacy = merged.get("minecraftArguments").str();
        if (!legacy.empty()) {
            std::string expanded = expandVars(legacy, vars);
            std::string tok;
            for (size_t i = 0; i < expanded.size(); i++) {
                char c = expanded[i];
                if (c == ' ' || c == '\t') {
                    if (!tok.empty()) { gameArgs.push_back(tok); tok.clear(); }
                } else {
                    tok += c;
                }
            }
            if (!tok.empty()) gameArgs.push_back(tok);
        }
    }

    // Assemble command line
    std::string cmdline = "\"" + java + "\" ";
    for (size_t i = 0; i < jvmArgs.size(); i++) cmdline += "\"" + jvmArgs[i] + "\" ";
    cmdline += "\"" + mainClass + "\" ";
    for (size_t i = 0; i < gameArgs.size(); i++) {
        if (gameArgs[i].empty()) continue;
        cmdline += "\"" + gameArgs[i] + "\" ";
    }

    logInfo("\u542F\u52A8 " + versionId + " (Java: " + java + ")");
    fireHook(EVENT_GAME_LAUNCHING, versionId.c_str(), NULL, 0);

    // Working directory = gameDir
    // Capture the game's stdout/stderr to a log file so crashes can be diagnosed
    std::string logDir = join(G->launcherDir, "logs");
    mkdirs(logDir);
    std::string gameLog = join(logDir, versionId + ".log");
    HANDLE logFile = CreateFileW(utf8tow(gameLog).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (logFile != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = logFile;
        si.hStdError = logFile;
    }
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    std::vector<char> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back('\0');

    if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL,
        instanceDir.c_str(), &si, &pi)) {
        logErr("\u542F\u52A8\u8FDB\u7A0B\u5931\u8D25, \u9519\u8BEF\u7801 " + std::to_string(GetLastError()));
        if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
        fireHook(EVENT_GAME_EXITED, versionId.c_str(), NULL, -1);
        return;
    }
    logInfo("\u6E38\u620F\u8FDB\u7A0B\u5DF2\u542F\u52A8 (PID " + std::to_string(pi.dwProcessId) + ")");
    logInfo("\u6E38\u620F\u65E5\u5FD7: " + gameLog);
    setStatus("\u6E38\u620F\u8FD0\u884C\u4E2D...");
    CloseHandle(pi.hThread);
    // Wait for exit (background thread, avoid blocking UI)
    HANDLE hProcess = pi.hProcess;
    std::thread waiter([hProcess, logFile, versionId]() {
        WaitForSingleObject(hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(hProcess, &code);
        CloseHandle(hProcess);
        if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
        logInfo("\u6E38\u620F\u5DF2\u9000\u51FA (\u9000\u51FA\u7801 " + std::to_string((int)code) + ")");
        setStatus("\u5C31\u7EEA");
        fireHook(EVENT_GAME_EXITED, versionId.c_str(), NULL, (int)code);
    });
    waiter.detach();
}

// ============================================================================
//  12. Mod download (Modrinth / direct link)
// ============================================================================

static std::string getModsDir(const std::string& versionId); // defined later

struct ModHit { std::string slug, title, desc; };

static bool searchModrinth(const std::string& query, std::vector<ModHit>& hits) {
    std::string url = "https://api.modrinth.com/v2/search?query=" + query +
        "&facets=%5B%5B%22project_type%3Amod%22%5D%5D&limit=20";
    Json j;
    if (!httpGetJson(url, j)) return false;
    if (!j.get("hits").isArr()) return false;
    for (size_t i = 0; i < j.get("hits").size(); i++) {
        ModHit h;
        h.slug = j.get("hits").at(i).get("slug").str();
        h.title = j.get("hits").at(i).get("title").str();
        h.desc = j.get("hits").at(i).get("description").str();
        hits.push_back(h);
    }
    return true;
}

static bool downloadModrinthVersion(const std::string& slug, const std::string& mcVersion, const std::string& loader) {
    std::string url = "https://api.modrinth.com/v2/project/" + slug + "/version?loaders=%5B%22" + loader + "%22%5D&game_versions=%5B%22" + mcVersion + "%22%5D";
    Json j;
    if (!httpGetJson(url, j)) return false;
    if (!j.isArr() || j.size() == 0) {
        // Fallback: no loader filter
        url = "https://api.modrinth.com/v2/project/" + slug + "/version";
        if (!httpGetJson(url, j) || !j.isArr() || j.size() == 0) return false;
    }
    const Json& v = j.at(0);
    std::string durl = v.get("files").at(0).get("url").str();
    std::string fname = v.get("files").at(0).get("filename").str();
    if (durl.empty() || fname.empty()) return false;
    std::string modsDir = getModsDir(mcVersion);
    mkdirs(modsDir);
    std::string label = "\u4E0B\u8F7D Mod " + fname;
    logInfo("\u4E0B\u8F7D Mod: " + fname);
    return downloadFile(durl, join(modsDir, fname), progressCb_global, &label);
}

// ============================================================================
//  13. GUI
// ============================================================================

enum {
    IDC_USER = 1001, IDC_PASS, IDC_COMBO_VER, IDC_COMBO_LOADER, IDC_LIST_INSTALLED,
    IDC_EDIT_JAVA, IDC_EDIT_MEM, IDC_LOGBOX, IDC_STATUS, IDC_EDIT_MODDIR,
    BTN_OFFLINE = 2001, BTN_MSA, BTN_REFRESH, BTN_INSTALL, BTN_LAUNCH,
    BTN_DELETE, BTN_MODS, BTN_THEME, BTN_SAVE,
    BTN_BROWSE_JAVA, BTN_OPEN_GAMEDIR, BTN_OPEN_VERDIR, BTN_MODLIST, BTN_COLOR,
    BTN_BROWSE_MODDIR, BTN_RESET_MODDIR,
    CHK_ISOLATION = 2101
};

#define PLUGIN_BTN_BASE 2500  // plugin toolbar buttons are created with ids starting here

static HFONT makeFont() {
    return CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
}

static void createBrushes() {
    if (G->brushBg) DeleteObject(G->brushBg);
    if (G->brushPanel) DeleteObject(G->brushPanel);
    if (G->brushList) DeleteObject(G->brushList);
    if (G->brushEdit) DeleteObject(G->brushEdit);
    if (G->brushLog) DeleteObject(G->brushLog);
    G->brushBg = CreateSolidBrush(G->theme.bg);
    G->brushPanel = CreateSolidBrush(G->theme.panel);
    G->brushList = CreateSolidBrush(G->theme.listBg);
    G->brushEdit = CreateSolidBrush(G->theme.panel);
    G->brushLog = CreateSolidBrush(G->theme.logBg);
}

static void applyThemeToControls(HWND h) {
    InvalidateRect(h, NULL, TRUE);
}

static void drawButton(HWND h, DRAWITEMSTRUCT* di) {
    COLORREF bg = G->theme.button;
    COLORREF fg = G->theme.buttonText;
    int id = (int)di->CtlID;
    bool primary = (id == BTN_INSTALL || id == BTN_LAUNCH || id == BTN_MSA);
    if (primary) { bg = G->theme.accent; fg = G->theme.accentText; }
    bool pressed = (di->itemState & ODS_SELECTED) != 0;
    if (pressed) {
        bg = RGB((GetRValue(bg) * 3) / 4, (GetGValue(bg) * 3) / 4, (GetBValue(bg) * 3) / 4);
    }
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    HBRUSH b = CreateSolidBrush(bg);
    HPEN p = CreatePen(PS_SOLID, 1, G->theme.border);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    HPEN op = (HPEN)SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 6, 6);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);

    wchar_t txt[256];
    GetWindowTextW(di->hwndItem, txt, 256);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    RECT tr = r;
    if (pressed) tr.top += 1;
    DrawTextW(dc, txt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void refreshInstalledList() {
    if (!G->listInstalled) return;
    SendMessageW(G->listInstalled, LB_RESETCONTENT, 0, 0);
    std::string vdir = join(G->gameDir, "versions");
    if (!dirExists(vdir)) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((vdir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::string id = fd.cFileName;
            std::string jp = join(join(vdir, id), id + ".json");
            if (fileExists(jp)) SendMessageA(G->listInstalled, LB_ADDSTRING, 0, (LPARAM)id.c_str());
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void populateComboVersions() {
    SendMessageW(G->comboVer, CB_RESETCONTENT, 0, 0);
    for (std::map<std::string, std::string>::const_iterator it = G->remoteVersions.begin(); it != G->remoteVersions.end(); ++it) {
        SendMessageA(G->comboVer, CB_ADDSTRING, 0, (LPARAM)it->first.c_str());
    }
    if (!G->remoteVersions.empty()) SendMessageW(G->comboVer, CB_SETCURSEL, 0, 0);
}

static std::string getSelectedVersion() {
    if (!G->comboVer) return "";
    int idx = (int)SendMessageW(G->comboVer, CB_GETCURSEL, 0, 0);
    if (idx < 0) return "";
    int len = (int)SendMessageW(G->comboVer, CB_GETLBTEXTLEN, idx, 0);
    if (len <= 0) return "";
    std::vector<char> buf(len + 1);
    SendMessageA(G->comboVer, CB_GETLBTEXT, idx, (LPARAM)buf.data());
    return std::string(buf.data());
}

static std::string getSelectedLoader() {
    if (!G->comboLoader) return "vanilla";
    int idx = (int)SendMessageW(G->comboLoader, CB_GETCURSEL, 0, 0);
    switch (idx) {
    case 1: return "forge";
    case 2: return "fabric";
    case 3: return "neoforge";
    case 4: return "quilt";
    default: return "vanilla";
    }
}

static std::string getSelectedInstalled() {
    if (!G->listInstalled) return "";
    int idx = (int)SendMessageW(G->listInstalled, LB_GETCURSEL, 0, 0);
    if (idx < 0) return "";
    int len = (int)SendMessageW(G->listInstalled, LB_GETTEXTLEN, idx, 0);
    if (len <= 0) return "";
    std::vector<char> buf(len + 1);
    SendMessageA(G->listInstalled, LB_GETTEXT, idx, (LPARAM)buf.data());
    return std::string(buf.data());
}

static void getEditText(HWND h, std::string& out) {
    int len = (int)GetWindowTextLengthW(h);
    if (len <= 0) { out = ""; return; }
    std::vector<wchar_t> buf(len + 1);
    GetWindowTextW(h, buf.data(), len + 1);
    out = wtoutf8(std::wstring(buf.data()));
}

// Background install
static void installWorker(const std::string& ver, const std::string& loader) {
    G->busy = true;
    bool ok = false;
    if (loader == "vanilla") ok = installVanilla(ver);
    else if (loader == "forge") ok = installForge(ver);
    else if (loader == "neoforge") ok = installNeoForge(ver);
    else if (loader == "fabric") ok = installFabric(ver);
    else if (loader == "quilt") ok = installQuilt(ver);
    G->busy = false;
    PostMessageW(G->hwnd, WM_APP + 20, ok ? 1 : 0, 0);
}

static void startInstall() {
    std::string ver = getSelectedVersion();
    std::string loader = getSelectedLoader();
    if (ver.empty()) { MessageBoxW(G->hwnd, L"\u8BF7\u5148\u5237\u65B0\u7248\u672C\u6E05\u5355\u5E76\u9009\u62E9\u7248\u672C", L"\u63D0\u793A", MB_ICONINFORMATION); return; }
    if (G->busy) { MessageBoxW(G->hwnd, L"\u5DF2\u6709\u4EFB\u52A1\u8FDB\u884C\u4E2D", L"\u63D0\u793A", MB_ICONINFORMATION); return; }
    logInfo("\u5F00\u59CB\u5B89\u88C5: " + ver + " (loader=" + loader + ")");
    std::thread t(installWorker, ver, loader);
    t.detach();
}

static void startLaunch() {
    std::string ver = getSelectedInstalled();
    if (ver.empty()) { MessageBoxW(G->hwnd, L"\u8BF7\u5148\u5728\u5217\u8868\u4E2D\u9009\u62E9\u4E00\u4E2A\u5DF2\u5B89\u88C5\u7248\u672C", L"\u63D0\u793A", MB_ICONINFORMATION); return; }
    // Sync settings field values (even if user has not saved)
    if (G->editJava) { std::string live; getEditText(G->editJava, live); if (!live.empty()) G->javaPath = live; }
    if (G->editMem) { std::string mem; getEditText(G->editMem, mem); int m = atoi(mem.c_str()); if (m >= 256) G->maxMemoryMB = m; }
    std::thread t(launchGame, ver);
    t.detach();
}

// Mod search dialog
struct ModDlgState { std::vector<ModHit> hits; HWND list; std::string mcVer; std::string loader; };

static INT_PTR CALLBACK ModDlgProc2(HWND h, UINT m, WPARAM w, LPARAM l) {
    static ModDlgState* s = NULL;
    switch (m) {
    case WM_INITDIALOG: {
        s = (ModDlgState*)l;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)l);
        s->list = GetDlgItem(h, 3001);
        return TRUE;
    }
    case WM_COMMAND: {
        switch (LOWORD(w)) {
        case 3002: {
            char q[256] = { 0 };
            GetDlgItemTextA(h, 3000, q, 255);
            s->hits.clear();
            SendMessageW(s->list, LB_RESETCONTENT, 0, 0);
            if (strlen(q) == 0) return TRUE;
            SetDlgItemTextW(h, 3003, utf8tow("\u641C\u7D22\u4E2D...").c_str());
            std::vector<ModHit> hits;
            if (searchModrinth(q, hits)) {
                s->hits = hits;
                for (size_t i = 0; i < hits.size(); i++) {
                    std::string line = hits[i].title + "  (" + hits[i].slug + ")";
                    SendMessageW(s->list, LB_ADDSTRING, 0, (LPARAM)utf8tow(line).c_str());
                }
                std::string cnt = "\u5171 " + std::to_string(hits.size()) + " \u4E2A\u7ED3\u679C";
                SetDlgItemTextW(h, 3003, utf8tow(cnt).c_str());
            } else {
                SetDlgItemTextW(h, 3003, utf8tow("\u641C\u7D22\u5931\u8D25").c_str());
            }
            return TRUE;
        }
        case 3004: { // Download selected
            int idx = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
            if (idx < 0) { SetDlgItemTextW(h, 3003, utf8tow("\u8BF7\u5148\u9009\u62E9").c_str()); return TRUE; }
            if (idx >= (int)s->hits.size()) return TRUE;
            SetDlgItemTextW(h, 3003, utf8tow("\u4E0B\u8F7D\u4E2D...").c_str());
            bool ok = downloadModrinthVersion(s->hits[idx].slug, s->mcVer, s->loader);
            SetDlgItemTextW(h, 3003, utf8tow(ok ? "\u4E0B\u8F7D\u5B8C\u6210" : "\u4E0B\u8F7D\u5931\u8D25").c_str());
            return TRUE;
        }
        case IDCANCEL: EndDialog(h, 0); return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static INT_PTR showModDialog();

// Theme rotation
static Theme themes[] = {
    { RGB(28,30,38), RGB(38,41,52), RGB(230,232,238), RGB(64,169,255), RGB(255,255,255), RGB(52,56,70), RGB(225,228,235), RGB(32,35,45), RGB(225,228,235), RGB(22,24,31), RGB(190,196,208), RGB(70,75,90) },  // Dark blue
    { RGB(24,26,27), RGB(36,39,40), RGB(235,237,238), RGB(80,200,120), RGB(255,255,255), RGB(46,50,51), RGB(230,233,234), RGB(30,33,34), RGB(230,233,234), RGB(18,20,21), RGB(200,206,208), RGB(80,86,88) },  // Dark green
    { RGB(245,246,248), RGB(255,255,255), RGB(40,44,52), RGB(24,119,242), RGB(255,255,255), RGB(236,239,243), RGB(40,44,52), RGB(255,255,255), RGB(40,44,52), RGB(250,251,252), RGB(90,96,105), RGB(200,205,212) },  // Light
};
static int themeIndex = 0;

static void refreshThemeUI() {
    createBrushes();
    if (G->hwnd) {
        InvalidateRect(G->hwnd, NULL, TRUE);
        EnumChildWindows(G->hwnd, [](HWND c, LPARAM) -> BOOL { InvalidateRect(c, NULL, TRUE); return TRUE; }, 0);
    }
}

static void cycleTheme() {
    themeIndex = (themeIndex + 1) % 3;
    G->theme = themes[themeIndex];
    refreshThemeUI();
    saveConfig();
}

// ---- App icon (generated at runtime) ----
static HICON createAppIcon() {
    int size = 32;
    HDC hdc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, size, size);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
    RECT r = { 0, 0, size, size };
    HBRUSH bg = CreateSolidBrush(RGB(94, 170, 74));
    FillRect(mem, &r, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(45, 85, 35));
    HPEN oldp = (HPEN)SelectObject(mem, pen);
    Rectangle(mem, 1, 1, size - 1, size - 1);
    SelectObject(mem, oldp);
    DeleteObject(pen);
    SetTextColor(mem, RGB(255, 255, 255));
    SetBkMode(mem, TRANSPARENT);
    HFONT f = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    HFONT of = (HFONT)SelectObject(mem, f);
    DrawTextW(mem, L"MC", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, of);
    DeleteObject(f);
    SelectObject(mem, old);
    HBITMAP mask = CreateCompatibleBitmap(hdc, size, size);
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, hdc);
    return icon;
}

// ---- Smooth rounded window ----
static void applyRoundedWindow(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, hgt + 1, 22, 22);
    SetWindowRgn(h, rgn, TRUE);
}

// ---- Open folder ----
static void openFolder(const std::string& dir) {
    mkdirs(dir);
    ShellExecuteW(NULL, L"open", utf8tow(dir).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ---- Browse for java.exe ----
static void browseForJava(HWND owner) {
    wchar_t buf[1024] = { 0 };
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Java \u53EF\u6267\u884C\u6587\u4EF6 (java.exe|javaw.exe)\0*.exe\0\u6240\u6709\u6587\u4EF6\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        G->javaPath = wtoutf8(buf);
        if (G->editJava) SetWindowTextW(G->editJava, utf8tow(G->javaPath).c_str());
        saveConfig();
        logInfo("\u5DF2\u9009\u62E9 Java: " + G->javaPath);
    }
}

// ---- Browse for mod install directory ----
static void browseForModDir(HWND owner) {
    wchar_t path[MAX_PATH] = { 0 };
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = L"\u9009\u62E9 Mod \u5B89\u88C5\u76EE\u5F55";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) {
            G->modDir = wtoutf8(path);
            if (G->editModDir) SetWindowTextW(G->editModDir, utf8tow(G->modDir).c_str());
            saveConfig();
            logInfo("\u5DF2\u9009\u62E9 Mod \u76EE\u5F55: " + G->modDir);
        }
        CoTaskMemFree(pidl);
    }
}

// ---- Version isolation directories ----
static std::string getInstanceDir(const std::string& versionId) {
    if (G->versionIsolation) return join(G->launcherDir, "instances\\" + versionId);
    return G->gameDir;
}
static std::string getModsDir(const std::string& versionId) {
    if (!G->modDir.empty()) return G->modDir;   // user-selected custom mod path wins
    if (G->versionIsolation) return join(getInstanceDir(versionId), "mods");
    return join(G->gameDir, "mods");
}

// ---- Color customization dialog ----
static COLORREF& themeColor(int i) {
    switch (i) {
    case 0: return G->theme.bg;
    case 1: return G->theme.panel;
    case 2: return G->theme.text;
    case 3: return G->theme.accent;
    case 4: return G->theme.accentText;
    case 5: return G->theme.button;
    case 6: return G->theme.buttonText;
    case 7: return G->theme.listBg;
    case 8: return G->theme.listText;
    case 9: return G->theme.logBg;
    case 10: return G->theme.logText;
    default: return G->theme.border;
    }
}
static const wchar_t* themeColorName(int i) {
    static const wchar_t* names[] = {
        L"\u80CC\u666F\u8272", L"\u9762\u677F\u8272", L"\u6587\u5B57\u8272", L"\u5F3A\u8C03\u8272", L"\u5F3A\u8C03\u6587\u5B57\u8272",
        L"\u6309\u94AE\u8272", L"\u6309\u94AE\u6587\u5B57\u8272", L"\u5217\u8868\u80CC\u666F\u8272", L"\u5217\u8868\u6587\u5B57\u8272", L"\u65E5\u5FD7\u80CC\u666F\u8272", L"\u65E5\u5FD7\u6587\u5B57\u8272", L"\u8FB9\u6846\u8272"
    };
    return names[i];
}

static INT_PTR CALLBACK ColorDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        HWND list = GetDlgItem(h, 4001);
        for (int i = 0; i < 12; i++) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)themeColorName(i));
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 4002: { // Edit selected color
            HWND list = GetDlgItem(h, 4001);
            int idx = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
            if (idx < 0 || idx >= 12) return TRUE;
            COLORREF c = themeColor(idx);
            static COLORREF custom[16] = { 0 };
            CHOOSECOLORW cc;
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = h;
            cc.rgbResult = c;
            cc.lpCustColors = custom;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&cc)) {
                themeColor(idx) = cc.rgbResult;
                refreshThemeUI();
            }
            return TRUE;
        }
        case 4003: { // Apply and save
            refreshThemeUI();
            saveConfig();
            logInfo("\u914D\u8272\u5DF2\u4FDD\u5B58");
            MessageBoxW(h, L"\u914D\u8272\u5DF2\u4FDD\u5B58\u5230\u914D\u7F6E\u6587\u4EF6", L"\u63D0\u793A", MB_OK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void buildColorDialogTemplate(std::vector<unsigned char>& out) {
    out.clear();
    auto pushW = [&](uint16_t x) { out.push_back(x & 0xff); out.push_back(x >> 8); };
    auto pushD = [&](uint32_t x) { for (int i = 0; i < 4; i++) out.push_back((x >> (i * 8)) & 0xff); };
    auto pushStr = [&](const wchar_t* s) { size_t n = wcslen(s) + 1; for (size_t i = 0; i < n; i++) { out.push_back(s[i] & 0xff); out.push_back(s[i] >> 8); } };
    auto align4 = [&]() { while (out.size() % 4) out.push_back(0); };

    pushD(0x80C808C0); // Dialog style
    pushD(0);
    pushW(3); // 3 controls
    pushW(10); pushW(10); pushW(320); pushW(240);
    pushW(0); pushW(0);
    pushStr(L"\u81EA\u5B9A\u4E49\u914D\u8272 (\u9009\u4E2D\u540E\u70B9\u4FEE\u6539, \u7528 RGB \u53D6\u8272)");
    pushW(9); pushStr(L"Microsoft YaHei UI");

    // Control 1: color list
    align4();
    pushD(0x50810101); pushD(0);
    pushW(10); pushW(10); pushW(200); pushW(220);
    pushW(4001); pushW(0xffff); pushW(0x0083);
    pushW(0); pushW(0);
    // Control 2: edit color button
    align4();
    pushD(0x50010000); pushD(0);
    pushW(220); pushW(10); pushW(90); pushW(28);
    pushW(4002); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u4FEE\u6539\u989C\u8272"); pushW(0);
    // Control 3: apply and save button
    align4();
    pushD(0x50010000); pushD(0);
    pushW(220); pushW(48); pushW(90); pushW(28);
    pushW(4003); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u5E94\u7528\u5E76\u4FDD\u5B58"); pushW(0);
    out[8] = 3; out[9] = 0;
}

static void showColorDialog() {
    std::vector<unsigned char> tmpl;
    buildColorDialogTemplate(tmpl);
    DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPDLGTEMPLATE)tmpl.data(), G->hwnd, ColorDlgProc, 0);
}

// ---- Mod list dialog ----
static INT_PTR CALLBACK ModListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static std::string modDir;
    switch (m) {
    case WM_INITDIALOG: {
        modDir = (const char*)l;
        HWND list = GetDlgItem(h, 5001);
        WIN32_FIND_DATAW fd;
        std::wstring pattern = utf8tow(join(modDir, "*.jar"));
        HANDLE fh = FindFirstFileW(pattern.c_str(), &fd);
        if (fh != INVALID_HANDLE_VALUE) {
            do {
                std::wstring name(fd.cFileName);
                if (name == L"." || name == L"..") continue;
                SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name.c_str());
            } while (FindNextFileW(fh, &fd));
            FindClose(fh);
        }
        int cnt = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
        SetDlgItemTextW(h, 5003, utf8tow("\u5171 " + std::to_string(cnt) + " \u4E2A Mod").c_str());
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 5002:
            ShellExecuteW(h, L"open", utf8tow(modDir).c_str(), NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void buildModListDialogTemplate(std::vector<unsigned char>& out) {
    out.clear();
    auto pushW = [&](uint16_t x) { out.push_back(x & 0xff); out.push_back(x >> 8); };
    auto pushD = [&](uint32_t x) { for (int i = 0; i < 4; i++) out.push_back((x >> (i * 8)) & 0xff); };
    auto pushStr = [&](const wchar_t* s) { size_t n = wcslen(s) + 1; for (size_t i = 0; i < n; i++) { out.push_back(s[i] & 0xff); out.push_back(s[i] >> 8); } };
    auto align4 = [&]() { while (out.size() % 4) out.push_back(0); };

    pushD(0x80C808C0); pushD(0);
    pushW(3);
    pushW(10); pushW(10); pushW(320); pushW(220);
    pushW(0); pushW(0);
    pushStr(L"\u5DF2\u5B89\u88C5\u7684 Mod \u5217\u8868");
    pushW(9); pushStr(L"Microsoft YaHei UI");

    // Control 1: mod list
    align4();
    pushD(0x50810101); pushD(0);
    pushW(10); pushW(10); pushW(300); pushW(170);
    pushW(5001); pushW(0xffff); pushW(0x0083);
    pushW(0); pushW(0);
    // Control 2: open Mods directory
    align4();
    pushD(0x50010000); pushD(0);
    pushW(10); pushW(190); pushW(140); pushW(26);
    pushW(5002); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u6253\u5F00 Mods \u76EE\u5F55"); pushW(0);
    // Control 3: status text
    align4();
    pushD(0x50000000); pushD(0);
    pushW(160); pushW(195); pushW(150); pushW(20);
    pushW(5003); pushW(0xffff); pushW(0x0082);
    pushStr(L""); pushW(0);
    out[8] = 3; out[9] = 0;
}

static void showModListDialog(const std::string& versionId) {
    std::string modDir = getModsDir(versionId);
    std::vector<unsigned char> tmpl;
    buildModListDialogTemplate(tmpl);
    DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPDLGTEMPLATE)tmpl.data(), G->hwnd, ModListProc, (LPARAM)modDir.c_str());
}

// Control creation helper
static HWND mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, HWND parent, int id, HINSTANCE inst) {
    return CreateWindowExW(0, cls, txt, style | WS_VISIBLE | WS_CHILD, x, y, w, h, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HINSTANCE inst = GetModuleHandleW(NULL);
        G->font = makeFont();
        int y = 10;
        // Account area
        mk(L"STATIC", L"\u8D26\u53F7\u540D:", WS_VISIBLE | WS_CHILD, 12, y + 4, 70, 20, h, 0, inst);
        G->editUser = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 80, y, 150, 24, h, IDC_USER, inst);
        G->editPass = mk(L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL, 240, y, 120, 24, h, IDC_PASS, inst);
        mk(L"BUTTON", L"\u79BB\u7EBF\u767B\u5F55", BS_OWNERDRAW, 370, y, 80, 26, h, BTN_OFFLINE, inst);
        mk(L"BUTTON", L"\u5FAE\u8F6F\u767B\u5F55", BS_OWNERDRAW, 456, y, 80, 26, h, BTN_MSA, inst);
        y += 40;
        // Version area
        mk(L"STATIC", L"\u7248\u672C:", WS_VISIBLE | WS_CHILD, 12, y + 6, 50, 20, h, 0, inst);
        G->comboVer = mk(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL, 60, y + 2, 160, 200, h, IDC_COMBO_VER, inst);
        mk(L"STATIC", L"\u52A0\u8F7D\u5668:", WS_VISIBLE | WS_CHILD, 230, y + 6, 60, 20, h, 0, inst);
        G->comboLoader = mk(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 290, y + 2, 90, 200, h, IDC_COMBO_LOADER, inst);
        mk(L"BUTTON", L"\u5237\u65B0", BS_OWNERDRAW, 390, y, 60, 26, h, BTN_REFRESH, inst);
        mk(L"BUTTON", L"\u5B89\u88C5", BS_OWNERDRAW, 456, y, 80, 26, h, BTN_INSTALL, inst);
        SendMessageW(G->comboLoader, CB_ADDSTRING, 0, (LPARAM)L"\u539F\u7248");
        SendMessageW(G->comboLoader, CB_ADDSTRING, 0, (LPARAM)L"Forge");
        SendMessageW(G->comboLoader, CB_ADDSTRING, 0, (LPARAM)L"Fabric");
        SendMessageW(G->comboLoader, CB_ADDSTRING, 0, (LPARAM)L"NeoForge");
        SendMessageW(G->comboLoader, CB_ADDSTRING, 0, (LPARAM)L"Quilt");
        SendMessageW(G->comboLoader, CB_SETCURSEL, 0, 0);
        y += 40;
        // Installed list
        mk(L"STATIC", L"\u5DF2\u5B89\u88C5:", WS_VISIBLE | WS_CHILD, 12, y + 4, 60, 20, h, 0, inst);
        G->listInstalled = mk(L"LISTBOX", L"", WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 12, y + 26, 300, 150, h, IDC_LIST_INSTALLED, inst);
        mk(L"BUTTON", L"\u542F\u52A8\u6E38\u620F", BS_OWNERDRAW, 330, y + 26, 90, 28, h, BTN_LAUNCH, inst);
        mk(L"BUTTON", L"\u5220\u9664\u7248\u672C", BS_OWNERDRAW, 330, y + 58, 90, 20, h, BTN_DELETE, inst);
        mk(L"BUTTON", L"\u4E0B\u8F7DMod", BS_OWNERDRAW, 330, y + 80, 90, 20, h, BTN_MODS, inst);
        mk(L"BUTTON", L"Mod\u5217\u8868", BS_OWNERDRAW, 330, y + 102, 90, 20, h, BTN_MODLIST, inst);
        mk(L"BUTTON", L"\u6253\u5F00\u6E38\u620F\u76EE\u5F55", BS_OWNERDRAW, 330, y + 124, 90, 20, h, BTN_OPEN_GAMEDIR, inst);
        mk(L"BUTTON", L"\u6253\u5F00\u7248\u672C\u76EE\u5F55", BS_OWNERDRAW, 330, y + 146, 90, 20, h, BTN_OPEN_VERDIR, inst);
        mk(L"BUTTON", L"\u81EA\u5B9A\u4E49\u914D\u8272", BS_OWNERDRAW, 330, y + 168, 90, 20, h, BTN_COLOR, inst);
        y += 190;
        // Settings
        mk(L"STATIC", L"Java:", WS_VISIBLE | WS_CHILD, 12, y + 6, 50, 20, h, 0, inst);
        G->editJava = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 60, y + 2, 250, 24, h, IDC_EDIT_JAVA, inst);
        mk(L"BUTTON", L"\u6D4F\u89C8", BS_OWNERDRAW, 316, y, 46, 26, h, BTN_BROWSE_JAVA, inst);
        mk(L"STATIC", L"\u5185\u5B58MB:", WS_VISIBLE | WS_CHILD, 370, y + 6, 60, 20, h, 0, inst);
        G->editMem = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 430, y + 2, 60, 24, h, IDC_EDIT_MEM, inst);
        mk(L"BUTTON", L"\u4FDD\u5B58", BS_OWNERDRAW, 496, y, 46, 26, h, BTN_SAVE, inst);
        y += 34;
        // Mod install path
        mk(L"STATIC", L"Mod\u8DEF\u5F84:", WS_VISIBLE | WS_CHILD, 12, y + 6, 60, 20, h, 0, inst);
        G->editModDir = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 72, y + 2, 380, 24, h, IDC_EDIT_MODDIR, inst);
        mk(L"BUTTON", L"\u6D4F\u89C8", BS_OWNERDRAW, 458, y, 46, 26, h, BTN_BROWSE_MODDIR, inst);
        mk(L"BUTTON", L"\u9ED8\u8BA4", BS_OWNERDRAW, 508, y, 46, 26, h, BTN_RESET_MODDIR, inst);
        y += 34;
        // Version isolation toggle
        mk(L"BUTTON", L"\u7248\u672C\u9694\u79BB(\u6BCF\u7248\u672C\u72EC\u7ACB\u5B58\u6863/Mods/\u914D\u7F6E)", BS_AUTOCHECKBOX | BS_LEFT | WS_TABSTOP, 12, y + 2, 250, 22, h, CHK_ISOLATION, inst);
        if (G->versionIsolation) SendMessageW(GetDlgItem(h, CHK_ISOLATION), BM_SETCHECK, BST_CHECKED, 0);
        y += 30;
        // Status
        G->statusLabel = mk(L"STATIC", L"\u5C31\u7EEA", WS_VISIBLE | WS_CHILD, 12, y + 2, 530, 20, h, IDC_STATUS, inst);
        y += 26;
        // Log
        G->logBox = mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOSEL, 12, y, 530, 150, h, IDC_LOGBOX, inst);

        // Plugin toolbar buttons (registered via add_button)
        int pbtnX = 12;
        int pbtnY = y + 156;
        for (size_t i = 0; i < G->pluginButtons.size(); i++) {
            std::wstring wlabel = utf8tow(G->pluginButtons[i].label);
            mk(L"BUTTON", wlabel.c_str(), BS_OWNERDRAW, pbtnX, pbtnY, 90, 26, h, PLUGIN_BTN_BASE + (int)i, inst);
            pbtnX += 98;
        }

        SetWindowTextW(G->editJava, utf8tow(G->javaPath).c_str());
        char mb[16]; snprintf(mb, sizeof(mb), "%d", G->maxMemoryMB);
        SetWindowTextA(G->editMem, mb);
        SetWindowTextW(G->editUser, utf8tow(G->username).c_str());
        SetWindowTextW(G->editModDir, utf8tow(G->modDir).c_str());

        // Apply font
        EnumChildWindows(h, [](HWND c, LPARAM) -> BOOL { SendMessageW(c, WM_SETFONT, (WPARAM)G->font, TRUE); return TRUE; }, 0);

        createBrushes();
        SetTimer(h, 1, 200, NULL);
        // Refresh manifest in background
        std::thread([]() { if (refreshManifest()) PostMessageW(G->hwnd, WM_APP + 12, 0, 0); }).detach();
        refreshInstalledList();
        fireHook(EVENT_LAUNCHER_START, "start", NULL, 0);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetTextColor(dc, G->theme.text);
        SetBkColor(dc, G->theme.bg);
        return (LRESULT)G->brushBg;
    }
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)w;
        SetTextColor(dc, G->theme.text);
        SetBkColor(dc, G->theme.bg);
        return (LRESULT)G->brushBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, G->theme.text);
        SetBkColor(dc, G->theme.panel);
        return (LRESULT)G->brushEdit;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w;
        if ((HWND)l == G->logBox) {
            SetTextColor(dc, G->theme.logText);
            SetBkColor(dc, G->theme.logBg);
            return (LRESULT)G->brushLog;
        }
        SetTextColor(dc, G->theme.listText);
        SetBkColor(dc, G->theme.listBg);
        return (LRESULT)G->brushList;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (di->CtlType == ODT_BUTTON) { drawButton(h, di); return TRUE; }
        break;
    }
    case WM_ERASEBKGND: {
        RECT r; GetClientRect(h, &r);
        HBRUSH b = CreateSolidBrush(G->theme.bg);
        FillRect((HDC)w, &r, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        // Custom-drawn progress bar
        RECT r;
        GetClientRect(h, &r);
        int py = r.bottom - 8;
        RECT bar = { 12, py, r.right - 12, py + 5 };
        HBRUSH bgb = CreateSolidBrush(G->theme.panel);
        FillRect(dc, &bar, bgb);
        DeleteObject(bgb);
        int pv, pm;
        { std::lock_guard<std::mutex> lk(G->mtx); pv = G->progress; pm = G->progressMax; }
        if (pm > 0) {
            RECT fill = bar;
            fill.right = bar.left + (long)((bar.right - bar.left) * (double)pv / pm);
            if (fill.right > bar.left) {
                HBRUSH fb = CreateSolidBrush(G->theme.accent);
                FillRect(dc, &fill, fb);
                DeleteObject(fb);
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER: {
        // Refresh log/status
        std::vector<std::string> lines;
        std::string status;
        { std::lock_guard<std::mutex> lk(G->mtx);
          while (!G->logQueue.empty()) { lines.push_back(G->logQueue.front()); G->logQueue.pop_front(); }
          status = G->statusText; }
        for (size_t i = 0; i < lines.size(); i++) {
            SendMessageW(G->logBox, LB_ADDSTRING, 0, (LPARAM)utf8tow(lines[i]).c_str());
            int cnt = (int)SendMessageW(G->logBox, LB_GETCOUNT, 0, 0);
            if (cnt > 500) SendMessageW(G->logBox, LB_DELETESTRING, 0, 0);
            SendMessageW(G->logBox, LB_SETTOPINDEX, 0, (WPARAM)(cnt - 1));
        }
        if (!status.empty()) SetWindowTextW(G->statusLabel, utf8tow(status).c_str());
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    case WM_APP + 10: { // Show Microsoft code
        std::string code, uri;
        { std::lock_guard<std::mutex> lk(G->mtx); code = G->msCode; uri = G->msVerifyUri; }
        std::string msg = "\u8BF7\u7528\u6D4F\u89C8\u5668\u6253\u5F00:\n" + uri + "\n\n\u5E76\u8F93\u5165\u4EE3\u7801:\n" + code;
        MessageBoxW(h, utf8tow(msg).c_str(), L"\u5FAE\u8F6F\u767B\u5F55", MB_OK);
        G->msAck = true;
        return 0;
    }
    case WM_APP + 11: {
        SetWindowTextW(G->editUser, utf8tow(G->username).c_str());
        return 0;
    }
    case WM_APP + 12: {
        populateComboVersions();
        return 0;
    }
    case WM_APP + 20: {
        refreshInstalledList();
        if (w == 0) logErr("\u5B89\u88C5\u5931\u8D25");
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case BTN_OFFLINE: {
            std::string name;
            getEditText(G->editUser, name);
            loginOffline(name);
            return 0;
        }
        case BTN_MSA:
            startMicrosoftLogin();
            return 0;
        case BTN_REFRESH:
            std::thread([]() { if (refreshManifest()) PostMessageW(G->hwnd, WM_APP + 12, 0, 0); }).detach();
            return 0;
        case BTN_INSTALL:
            startInstall();
            return 0;
        case BTN_LAUNCH:
            startLaunch();
            return 0;
        case BTN_DELETE: {
            std::string ver = getSelectedInstalled();
            if (ver.empty()) return 0;
            std::string msg = "\u786E\u5B9A\u5220\u9664\u7248\u672C " + ver + " \u5417?\n(\u4EC5\u5220\u9664\u8BE5\u7248\u672C\u7684\u76EE\u5F55)";
            if (MessageBoxW(h, utf8tow(msg).c_str(), L"\u786E\u8BA4", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                std::string d = join(G->gameDir, "versions\\" + ver);
                // Safely delete this version directory
                std::string cmd = "cmd /c rmdir /s /q \"" + d + "\"";
                system(cmd.c_str());
                logInfo("\u5DF2\u5220\u9664\u7248\u672C: " + ver);
                refreshInstalledList();
            }
            return 0;
        }
        case BTN_MODS:
            showModDialog();
            return 0;
        case BTN_MODLIST: {
            std::string ver = getSelectedInstalled();
            if (ver.empty()) { MessageBoxW(h, L"\u8BF7\u5148\u9009\u62E9\u7248\u672C", L"\u63D0\u793A", MB_ICONINFORMATION); return 0; }
            showModListDialog(ver);
            return 0;
        }
        case BTN_OPEN_GAMEDIR:
            openFolder(getInstanceDir(getSelectedInstalled()));
            return 0;
        case BTN_OPEN_VERDIR: {
            std::string ver = getSelectedInstalled();
            if (ver.empty()) return 0;
            openFolder(join(G->gameDir, "versions\\" + ver));
            return 0;
        }
        case BTN_BROWSE_JAVA:
            browseForJava(h);
            return 0;
        case BTN_BROWSE_MODDIR:
            browseForModDir(h);
            return 0;
        case BTN_RESET_MODDIR:
            G->modDir.clear();
            if (G->editModDir) SetWindowTextW(G->editModDir, L"");
            saveConfig();
            logInfo("\u5DF2\u6062\u590D\u9ED8\u8BA4 Mod \u76EE\u5F55");
            return 0;
        case BTN_COLOR:
            showColorDialog();
            return 0;
        case BTN_THEME:
            cycleTheme();
            return 0;
        case CHK_ISOLATION: {
            G->versionIsolation = (SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED);
            saveConfig();
            logInfo(G->versionIsolation ? "\u7248\u672C\u9694\u79BB\u5DF2\u5F00\u542F" : "\u7248\u672C\u9694\u79BB\u5DF2\u5173\u95ED");
            return 0;
        }
        case BTN_SAVE: {
            getEditText(G->editJava, G->javaPath);
            std::string mem;
            getEditText(G->editMem, mem);
            G->maxMemoryMB = atoi(mem.c_str());
            if (G->maxMemoryMB < 256) G->maxMemoryMB = 256;
            getEditText(G->editUser, G->username);
            getEditText(G->editModDir, G->modDir);
            saveConfig();
            logInfo("\u8BBE\u7F6E\u5DF2\u4FDD\u5B58");
            MessageBoxW(h, L"\u8BBE\u7F6E\u5DF2\u4FDD\u5B58", L"\u63D0\u793A", MB_OK);
            return 0;
        }
        case IDC_LIST_INSTALLED:
            if (HIWORD(w) == LBN_DBLCLK) startLaunch();
            return 0;
        default: {
            int btnId = LOWORD(w);
            if (btnId >= PLUGIN_BTN_BASE && btnId < PLUGIN_BTN_BASE + (int)G->pluginButtons.size()) {
                int idx = btnId - PLUGIN_BTN_BASE;
                PluginEvent ev;
                ev.type = 0;
                ev.message = G->pluginButtons[idx].label.c_str();
                ev.data = NULL;
                ev.result = 0;
                G->pluginButtons[idx].fn(&ev);
                return 0;
            }
            break;
        }
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// Dialog resource template (build Mod search dialog in memory)
static void buildModDialogTemplate(std::vector<unsigned char>& out) {
    // DLGTEMPLATE + controls
    out.clear();
    auto align4 = [](std::vector<unsigned char>& v) { while (v.size() % 4) v.push_back(0); };
    auto pushW = [&](uint16_t x) { out.push_back(x & 0xff); out.push_back(x >> 8); };
    auto pushD = [&](uint32_t x) { for (int i = 0; i < 4; i++) out.push_back((x >> (i * 8)) & 0xff); };
    auto pushStr = [&](const wchar_t* s) {
        size_t n = wcslen(s) + 1;
        for (size_t i = 0; i < n; i++) { out.push_back(s[i] & 0xff); out.push_back(s[i] >> 8); }
    };

    // DLGTEMPLATE
    pushD(0x80C808C0); // DS_SETFONT|DS_MODALFRAME|DS_CENTER|WS_POPUP|WS_CAPTION|WS_SYSMENU
    pushD(0);
    pushW(3);  // 3 controls
    pushW(10); pushW(10); pushW(360); pushW(200); // x,y,cx,cy
    pushW(0);  // no menu
    pushW(0);  // default dialog class
    pushStr(L"\u4E0B\u8F7D Mod (Modrinth)");
    pushW(9); // Font size
    pushStr(L"Microsoft YaHei UI");

    // Control 1: search box EDIT
    align4(out);
    pushD(0x50810080); // WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL
    pushD(0);
    pushW(10); pushW(10); pushW(240); pushW(22);
    pushW(3000); pushW(0xffff); pushW(0x0081); // class EDIT
    pushW(0);
    pushW(0);
    // Control 2: search button BUTTON
    align4(out);
    pushD(0x50010000); // WS_CHILD|WS_VISIBLE|WS_TABSTOP
    pushD(0);
    pushW(260); pushW(9); pushW(80); pushW(24);
    pushW(3002); pushW(0xffff); pushW(0x0080); // BUTTON
    pushStr(L"\u641C\u7D22");
    pushW(0);
    // Control 3: list LISTBOX
    align4(out);
    pushD(0x50810101); // WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY
    pushD(0);
    pushW(10); pushW(40); pushW(240); pushW(120);
    pushW(3001); pushW(0xffff); pushW(0x0083); // LISTBOX
    pushW(0);
    pushW(0);
    // Control 4: download button BUTTON
    align4(out);
    pushD(0x50010000);
    pushD(0);
    pushW(260); pushW(40); pushW(90); pushW(28);
    pushW(3004); pushW(0xffff); pushW(0x0080);
    pushStr(L"\u4E0B\u8F7D\u9009\u4E2D");
    pushW(0);
    // Control 5: status STATIC
    align4(out);
    pushD(0x50000000); // WS_CHILD|WS_VISIBLE
    pushD(0);
    pushW(10); pushW(170); pushW(330); pushW(20);
    pushW(3003); pushW(0xffff); pushW(0x0082); // STATIC
    pushStr(L"\u8F93\u5165\u5173\u952E\u8BCD\u641C\u7D22");
    pushW(0);

    // Fix control count: actually 5
    out[8] = 5; out[9] = 0;
}

// Since DialogBoxParamW needs resources, use DialogBoxIndirectParamW dynamic template
static INT_PTR showModDialog() {
    ModDlgState st;
    st.mcVer = getSelectedInstalled();
    st.loader = "fabric";
    if (st.mcVer.find("forge") != std::string::npos) st.loader = "forge";
    else if (st.mcVer.find("fabric") != std::string::npos) st.loader = "fabric";
    size_t dash = st.mcVer.find('-');
    if (dash != std::string::npos) st.mcVer = st.mcVer.substr(0, dash);
    std::vector<unsigned char> tmpl;
    buildModDialogTemplate(tmpl);
    return DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPDLGTEMPLATE)tmpl.data(), G->hwnd, ModDlgProc2, (LPARAM)&st);
}

// ============================================================================
//  14. WinMain
// ============================================================================

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmd) {
    // Initialize state
    static AppState app;
    G = &app;
    app.launcherDir = join(appDataDir(), "GDML");
    mkdirs(app.launcherDir);
    mkdirs(join(app.launcherDir, "plugins"));
    loadConfig();
    loadPlugins();

    // Register window class
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    HICON appIcon = G->customIcon;   // plugins may set a custom icon before the window exists
    if (!appIcon) appIcon = createAppIcon();
    if (!appIcon && !G->customIconPath.empty()) {
        appIcon = loadIconFromFile(G->customIconPath);
        if (appIcon) G->customIcon = appIcon;
    }
    wc.hIcon = appIcon;
    wc.lpszClassName = L"GDMLWnd";
    wc.hbrBackground = NULL;
    RegisterClassW(&wc);

    G->hwnd = CreateWindowExW(0, L"GDMLWnd", L"GDML \u542F\u52A8\u5668",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 730, NULL, NULL, hInst, NULL);
    if (!G->hwnd) return 1;
    SendMessageW(G->hwnd, WM_SETICON, ICON_BIG, (LPARAM)appIcon);
    SendMessageW(G->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)appIcon);
    applyRoundedWindow(G->hwnd);
    ShowWindow(G->hwnd, nCmd);
    UpdateWindow(G->hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Unload plugins
    for (size_t i = 0; i < G->plugins.size(); i++) {
        if (G->plugins[i].shutdown) G->plugins[i].shutdown();
        FreeLibrary(G->plugins[i].mod);
    }
    saveConfig();
    return (int)msg.wParam;
}

