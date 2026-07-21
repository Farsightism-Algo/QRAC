/******************************************************************
 * QRAC - Quantitative Random Access Codes
 * Copyright (c) 2026 Xuehaoyu Chen. MIT License.
 *
 * 项目地址: https://github.com/sans666VIP/QRAC
 *
 * 编译: g++ -std=c++17 qrac.cpp -o qrac
 *   Windows:   g++ -std=c++17 qrac.cpp -o qrac.exe -lcomdlg32 -lole32 -static
 *   Linux:     g++ -std=c++17 qrac.cpp -o qrac
 *
 * 第三方库 (放入同目录):
 *   stb_image.h / stb_image_write.h / stb_image_resize.h
 *   https://github.com/nothings/stb
 ******************************************************************/
#define NOMINMAX
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <limits>
#include <filesystem>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

namespace fs = std::filesystem;

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

// ============================================================
// 版本信息
// ============================================================
constexpr int QRAC_FORMAT_VERSION = 1;
const char* QRAC_SOFTWARE_VER = "5.3";

// ============================================================
// 原生文件对话框（跨平台，无第三方依赖）
// ============================================================
std::string nativeOpenFileDialog(const char* filterPattern = nullptr) {
#ifdef _WIN32
    char buf[MAX_PATH * 4] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filterPattern
        ? filterPattern
        : "All Files\0*.*\0QRAC Images\0*.png;*.bmp;*.jpg;*.jpeg\0\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameA(&ofn)) return std::string(buf);
    return {};
#else
    std::string cmd = "zenity --file-selection --title=\"Select File\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    char buf[4096] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);
    return result;
#endif
}

std::string nativeSaveFileDialog(const char* defaultName = "output.png") {
#ifdef _WIN32
    char buf[MAX_PATH * 4] = {};
    strncpy(buf, defaultName, sizeof(buf) - 1);
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "PNG\0*.png\0BMP\0*.bmp\0All\0*.*\0\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.nFilterIndex = 1;
    if (GetSaveFileNameA(&ofn)) return std::string(buf);
    return {};
#else
    std::string cmd = "zenity --file-selection --save --confirm-overwrite"
        " --title=\"Save File\" --filename=\"" +
        std::string(defaultName) + "\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    char buf[4096] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);
    return result;
#endif
}

// 打开文件夹对话框（用于批量编码）
std::string nativeOpenFolderDialog() {
#ifdef _WIN32
    char buf[MAX_PATH * 4] = {};
    BROWSEINFOA bi = {};
    bi.lpszTitle = "Select folder containing files to encode";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        SHGetPathFromIDListA(pidl, buf);
        CoTaskMemFree(pidl);
        return std::string(buf);
    }
    return {};
#else
    std::string cmd = "zenity --file-selection --directory"
        " --title=\"Select Folder\" 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    char buf[4096] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(pipe);
    return result;
#endif
}

// ============================================================
// 前向声明区 —— atomicWriteFile 依赖这些，必须定义在前面
// ============================================================

#ifdef _WIN32
std::wstring utf8ToWstring(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (!len) return {};
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}
#endif

enum class ErrorType {
    FileNotFound, FileReadError, FileWriteError,
    ImageLoadError, ImageSaveError, ImageSizeError,
    DataSizeError, FECError, UserAbort, InvalidInput, DamageExceeded
};

class QRACException : public std::runtime_error {
public:
    QRACException(ErrorType t, const std::string& m)
        : std::runtime_error(m), m_type(t) {}
    ErrorType getType() const { return m_type; }
private:
    ErrorType m_type;
};

// ============================================================
// 日志系统：同时输出到控制台和文件
// ============================================================
static std::ofstream g_logFile;

void initLogFile() {
    g_logFile.open("qrac.log", std::ios::app);
}

// 双重输出宏
#define LOG(x) do { \
    std::cout << x; \
    if (g_logFile.is_open()) g_logFile << x; \
} while(0)

void closeLogFile() {
    if (g_logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        g_logFile << "--- Session ended: " << std::ctime(&t);
        g_logFile.close();
    }
}

// ============================================================
// 原子文件写入：先写 .tmp，成功后再 rename
// ============================================================
void atomicWriteFile(const std::string& path,
    const void* data, size_t len,
    bool binary = true) {
    std::string tmpPath = path + ".tmp";
    {
#ifdef _WIN32
        std::ofstream f(utf8ToWstring(tmpPath),
            binary ? std::ios::binary : std::ios::out);
#else
        std::ofstream f(tmpPath, binary ? std::ios::binary : std::ios::out);
#endif
        if (!f.is_open())
            throw QRACException(ErrorType::FileWriteError,
                "Cannot create temp file: " + tmpPath);
        f.write(reinterpret_cast<const char*>(data), len);
        if (!f.good())
            throw QRACException(ErrorType::FileWriteError,
                "Write failed to: " + tmpPath);
        f.close();
    }
    // 替换原文件（同一文件系统内是原子的）
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        // rename 失败时尝试清理
        try { fs::remove(tmpPath); }
        catch (...) {}
        throw QRACException(ErrorType::FileWriteError,
            "Cannot finalize: " + path + " (" + ec.message() + ")");
    }
}

// ============================================================
// Config 持久化
// ============================================================
struct QRACConfig {
    int   L = 5;
    uint8_t FILLER_MAX_VALUE = 10;
    float FEC_REDUNDANCY_RATIO = 0.25f;
    int   MIN_IMAGE_DIMENSION = 16;
    int   DEFAULT_SMALL_SIZE = 128;
    int   DEFAULT_MEDIUM_SIZE = 512;
    int   DEFAULT_LARGE_SIZE = 1024;
    size_t SMALL_FILE_THRESHOLD = 96 * 1024;
    size_t MEDIUM_FILE_THRESHOLD = 1024 * 1024;
    int   SYMBOLS_PER_PIXEL = 3;
    int   FEC_BLOCK_SIZE = 10;
    int   MAX_FEC_WARNINGS = 15;
    float TEXT_DETECTION_THRESHOLD = 0.85f;
    float CONTROL_CHAR_THRESHOLD = 0.05f;
    bool  USE_ADVANCED_FEC = false;
    uint8_t DAMAGE_THRESHOLD = 3;
    // 运行时选项（不持久化到 header）
    bool  verifyRoundtrip = false;
    size_t largeFileWarningMB = 50;
};

void saveConfig(const QRACConfig& cfg) {
    std::ofstream f("qrac_config.ini");
    if (!f.is_open()) return;
    f << "# QRAC Config - auto-generated\n"
        << "L=" << cfg.L << "\n"
        << "FILLER_MAX_VALUE=" << (int)cfg.FILLER_MAX_VALUE << "\n"
        << "FEC_REDUNDANCY_RATIO=" << cfg.FEC_REDUNDANCY_RATIO << "\n"
        << "DAMAGE_THRESHOLD=" << (int)cfg.DAMAGE_THRESHOLD << "\n"
        << "MIN_IMAGE_DIMENSION=" << cfg.MIN_IMAGE_DIMENSION << "\n"
        << "DEFAULT_SMALL_SIZE=" << cfg.DEFAULT_SMALL_SIZE << "\n"
        << "DEFAULT_MEDIUM_SIZE=" << cfg.DEFAULT_MEDIUM_SIZE << "\n"
        << "DEFAULT_LARGE_SIZE=" << cfg.DEFAULT_LARGE_SIZE << "\n"
        << "SMALL_FILE_THRESHOLD=" << cfg.SMALL_FILE_THRESHOLD << "\n"
        << "MEDIUM_FILE_THRESHOLD=" << cfg.MEDIUM_FILE_THRESHOLD << "\n"
        << "verifyRoundtrip=" << (cfg.verifyRoundtrip ? 1 : 0) << "\n"
        << "largeFileWarningMB=" << cfg.largeFileWarningMB << "\n";
}

void loadConfig(QRACConfig& cfg) {
    std::ifstream f("qrac_config.ini");
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        try {
            if (k == "L") cfg.L = std::stoi(v);
            else if (k == "FILLER_MAX_VALUE") cfg.FILLER_MAX_VALUE = (uint8_t)std::stoi(v);
            else if (k == "FEC_REDUNDANCY_RATIO") cfg.FEC_REDUNDANCY_RATIO = std::stof(v);
            else if (k == "DAMAGE_THRESHOLD") cfg.DAMAGE_THRESHOLD = (uint8_t)std::stoi(v);
            else if (k == "MIN_IMAGE_DIMENSION") cfg.MIN_IMAGE_DIMENSION = std::stoi(v);
            else if (k == "DEFAULT_SMALL_SIZE") cfg.DEFAULT_SMALL_SIZE = std::stoi(v);
            else if (k == "DEFAULT_MEDIUM_SIZE") cfg.DEFAULT_MEDIUM_SIZE = std::stoi(v);
            else if (k == "DEFAULT_LARGE_SIZE") cfg.DEFAULT_LARGE_SIZE = std::stoi(v);
            else if (k == "SMALL_FILE_THRESHOLD") cfg.SMALL_FILE_THRESHOLD = std::stoull(v);
            else if (k == "MEDIUM_FILE_THRESHOLD") cfg.MEDIUM_FILE_THRESHOLD = std::stoull(v);
            else if (k == "verifyRoundtrip") cfg.verifyRoundtrip = (std::stoi(v) != 0);
            else if (k == "largeFileWarningMB") cfg.largeFileWarningMB = std::stoull(v);
        }
        catch (...) { /* 解析失败则保留默认值 */ }
    }
}

// ============================================================
// 图像布局常量
// ============================================================
constexpr int HEADER_PIXELS = 4;
constexpr int VERIFICATION_PIXELS = 5;
constexpr int HEADER_BYTES = HEADER_PIXELS * 3;

/*
 * Header 字节布局 (12 bytes total):
 *   [0..3]  dataSize (uint32 LE)
 *   [4]     L 参数
 *   [5]     FILLER_MAX_VALUE
 *   [6]     FEC_REDUNDANCY_RATIO * 100
 *   [7]     XOR checksum of bytes [0..6]
 *   [8]     Format version (QRAC_FORMAT_VERSION)
 *   [9..11] Reserved (zero)
 */

const uint8_t VERIFICATION_COLORS[VERIFICATION_PIXELS][3] = {
    {255,   0,   0}, {  0,   0, 255}, {  0, 255,   0},
    {255, 255,   0}, {255, 255, 255}
};

// ============================================================
// CRC32
// ============================================================
static uint32_t crc32_table[256];
static bool     crc32_table_ready = false;

static void buildCRC32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        crc32_table[i] = crc;
    }
    crc32_table_ready = true;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    if (!crc32_table_ready) buildCRC32Table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

void appendCRC32(std::vector<uint8_t>& data) {
    uint32_t cs = crc32(data.data(), data.size());
    data.push_back((cs >> 0) & 0xFF); data.push_back((cs >> 8) & 0xFF);
    data.push_back((cs >> 16) & 0xFF); data.push_back((cs >> 24) & 0xFF);
}

bool verifyAndStripCRC32(std::vector<uint8_t>& data) {
    if (data.size() < 4) return false;
    size_t pl = data.size() - 4;
    uint32_t stored = (uint32_t)data[pl] | ((uint32_t)data[pl + 1] << 8)
        | ((uint32_t)data[pl + 2] << 16) | ((uint32_t)data[pl + 3] << 24);
    uint32_t computed = crc32(data.data(), pl);
    data.resize(pl);
    return stored == computed;
}

// ============================================================
// 量化编解码核心
// ============================================================
int calculateIntervals(const QRACConfig& cfg) {
    int r = 256 - (cfg.FILLER_MAX_VALUE + 1);
    return r / cfg.L + (r % cfg.L ? 1 : 0);
}
bool isFillerValue(uint8_t v, const QRACConfig& cfg) { return v <= cfg.FILLER_MAX_VALUE; }
bool isFillerPixel(const uint8_t* p, const QRACConfig& cfg) {
    return isFillerValue(p[0], cfg) && isFillerValue(p[1], cfg) && isFillerValue(p[2], cfg);
}
int calculateAnchor(int idx, const QRACConfig& cfg) {
    int s = cfg.FILLER_MAX_VALUE + 1 + idx * cfg.L;
    int e = std::min(s + cfg.L - 1, 255);
    return s + (e - s) / 2;
}
int decodeToSymbol(uint8_t v, const QRACConfig& cfg) {
    if (isFillerValue(v, cfg)) return -1;
    int adj = v - (cfg.FILLER_MAX_VALUE + 1);
    int intervals = calculateIntervals(cfg);
    int idx = adj / cfg.L;
    return (idx >= intervals) ? intervals - 1 : idx;
}

// ---- 数据 ↔ 符号 转换链 ----
std::vector<bool> dataToBinary(const std::vector<uint8_t>& data) {
    std::vector<bool> bits; bits.reserve(data.size() * 8);
    for (uint8_t b : data)
        for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1);
    return bits;
}
std::vector<int> binaryToSymbols(const std::vector<bool>& bits,
    int bps, const QRACConfig& cfg) {
    int intervals = calculateIntervals(cfg);
    int n = (int)((bits.size() + bps - 1) / bps);
    std::vector<int> syms; syms.reserve(n);
    for (int i = 0; i < n; ++i) {
        int sym = 0;
        for (int j = 0; j < bps; ++j) {
            int bi = i * bps + j;
            sym = (sym << 1) | ((bi < (int)bits.size() && bits[bi]) ? 1 : 0);
        }
        syms.push_back(sym % intervals);
    }
    return syms;
}
std::vector<bool> symbolsToBinary(const std::vector<int>& syms,
    int bps, size_t expectedBits) {
    std::vector<bool> bits; bits.reserve(expectedBits);
    for (int sym : syms) {
        if (sym == -1) continue;
        for (int i = bps - 1; i >= 0; --i) {
            bits.push_back((sym >> i) & 1);
            if (bits.size() >= expectedBits) goto done;
        }
    }
done:
    if (bits.size() > expectedBits) bits.resize(expectedBits);
    return bits;
}
std::vector<uint8_t> binaryToData(const std::vector<bool>& bits) {
    std::vector<uint8_t> data; data.reserve((bits.size() + 7) / 8);
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8; ++j)
            b = (b << 1) | ((i + j < bits.size() && bits[i + j]) ? 1 : 0);
        data.push_back(b);
    }
    return data;
}

// ---- Header（含自校验和版本号）----
std::vector<uint8_t> buildHeader(uint32_t dataSize, const QRACConfig& cfg) {
    std::vector<uint8_t> h(HEADER_BYTES, 0);
    h[0] = dataSize & 0xFF; h[1] = (dataSize >> 8) & 0xFF;
    h[2] = (dataSize >> 16) & 0xFF; h[3] = (dataSize >> 24) & 0xFF;
    h[4] = (uint8_t)cfg.L; h[5] = cfg.FILLER_MAX_VALUE;
    h[6] = (uint8_t)(cfg.FEC_REDUNDANCY_RATIO * 100.0f + 0.5f);
    // byte 7: XOR checksum of bytes 0..6
    uint8_t ck = 0;
    for (int i = 0; i < 7; ++i) ck ^= h[i];
    h[7] = ck;
    // byte 8: format version
    h[8] = QRAC_FORMAT_VERSION;
    // bytes 9..11 保留
    return h;
}

struct DecodedHeader {
    uint32_t dataSize = 0; int L = 5; uint8_t fillerMax = 10;
    float fecRatio = 0.25f; int formatVer = 0; bool valid = false;
};

DecodedHeader parseHeader(const uint8_t* img) {
    DecodedHeader dh;
    dh.dataSize = (uint32_t)img[0] | ((uint32_t)img[1] << 8)
        | ((uint32_t)img[2] << 16) | ((uint32_t)img[3] << 24);
    dh.L = std::max(1, (int)img[4]); dh.fillerMax = img[5];
    dh.fecRatio = img[6] / 100.0f;
    // 验证 XOR checksum
    uint8_t ck = 0;
    for (int i = 0; i < 7; ++i) ck ^= img[i];
    if (ck != img[7]) { dh.valid = false; return dh; }
    dh.formatVer = img[8];
    // 基础合法性检查
    if (dh.L <= 50 && dh.fillerMax <= 200 && dh.dataSize < 0x80000000u)
        dh.valid = true;
    return dh;
}

QRACConfig configFromHeader(const DecodedHeader& dh) {
    QRACConfig c; c.L = dh.L; c.FILLER_MAX_VALUE = dh.fillerMax;
    c.FEC_REDUNDANCY_RATIO = dh.fecRatio; return c;
}

// ---- 最大符号数估算（编码前预检查用）----
int maxSymbolsInImage(int w, int h, const QRACConfig& cfg) {
    int dataPx = w * h - HEADER_PIXELS - VERIFICATION_PIXELS;
    return dataPx * cfg.SYMBOLS_PER_PIXEL;
}

// ---- 创建图像 ----
std::vector<uint8_t> createQRACImage(
    const std::vector<int>& symbols, int w, int h,
    const QRACConfig& cfg, const std::vector<uint8_t>& header)
{
    int total = w * h, dataPx = total - HEADER_PIXELS - VERIFICATION_PIXELS;
    int maxSyms = dataPx * cfg.SYMBOLS_PER_PIXEL;
    if ((int)symbols.size() > maxSyms) {
        std::ostringstream oss;
        oss << "Image too small: need " << symbols.size()
            << " symbols, can only fit " << maxSyms
            << " (" << w << "x" << h << "), "
            << "suggested min side: "
            << std::max(cfg.MIN_IMAGE_DIMENSION,
                (int)std::ceil(std::sqrt(
                    (symbols.size() + cfg.SYMBOLS_PER_PIXEL - 1)
                    / cfg.SYMBOLS_PER_PIXEL + HEADER_PIXELS + VERIFICATION_PIXELS)));
        throw QRACException(ErrorType::ImageSizeError, oss.str());
    }
    std::vector<uint8_t> img(total * 3, 0);
    for (int p = 0; p < HEADER_PIXELS; ++p)
        for (int ch = 0; ch < 3; ++ch) img[p * 3 + ch] = header[p * 3 + ch];
    int ds = HEADER_PIXELS;
    for (size_t i = 0; i < symbols.size(); ++i) {
        int po = ds + (int)i / cfg.SYMBOLS_PER_PIXEL;
        int ch = (int)i % cfg.SYMBOLS_PER_PIXEL;
        int row = po / w, col = po % w;
        if (row >= h) break;
        img[(row * w + col) * 3 + ch] = (uint8_t)calculateAnchor(symbols[i], cfg);
    }
    int vs = (total - VERIFICATION_PIXELS) * 3;
    for (int i = 0; i < VERIFICATION_PIXELS; ++i)
        for (int ch = 0; ch < 3; ++ch) img[vs + i * 3 + ch] = VERIFICATION_COLORS[i][ch];
    return img;
}

// ---- 损伤检测 ----
bool checkVerificationPixels(const uint8_t* img, int w, int h, int chs,
    const QRACConfig& cfg) {
    int total = w * h, vs = (total - VERIFICATION_PIXELS) * chs;
    bool bad = false;
    const char* names[] = { "Red","Blue","Green","Yellow","White" };
    LOG("Verification pixel analysis:\n");
    for (int i = 0; i < VERIFICATION_PIXELS; ++i) {
        int b = vs + i * chs, pixDmg = 0;
        for (int ch = 0; ch < 3; ++ch) {
            int diff = std::abs((int)img[b + ch] - (int)VERIFICATION_COLORS[i][ch]);
            pixDmg += diff;
            if (diff > (int)cfg.DAMAGE_THRESHOLD) {
                std::ostringstream oss;
                oss << "  " << names[i] << " ch" << ch << " damage=" << diff
                    << " (threshold=" << (int)cfg.DAMAGE_THRESHOLD << ")\n";
                LOG(oss.str());
            }
        }
        if (pixDmg > (int)cfg.DAMAGE_THRESHOLD * 3) bad = true;
    }
    std::ostringstream oss2;
    oss2 << "Cumulative threshold per pixel: " << (int)cfg.DAMAGE_THRESHOLD * 3 << "\n";
    LOG(oss2.str());
    return !bad;
}

// ---- 提取数据（新格式，从像素 HEADER_PIXELS 开始读）----
std::vector<uint8_t> extractRawDataFromImage(
    const uint8_t* img, int w, int h, int chs,
    const QRACConfig& cfg, bool& damageOk)
{
    damageOk = checkVerificationPixels(img, w, h, chs, cfg);
    if (!damageOk) LOG("Warning: image damage exceeds threshold.\n");
    int total = w * h, dataPx = total - HEADER_PIXELS - VERIFICATION_PIXELS;
    int totalSyms = dataPx * cfg.SYMBOLS_PER_PIXEL;
    int intervals = calculateIntervals(cfg), bps = (int)std::log2(intervals);
    std::vector<int> syms; syms.reserve(totalSyms);
    int ds = HEADER_PIXELS * chs, de = ds + dataPx * chs;
    for (int i = ds; i < de; i += chs) {
        if (isFillerPixel(&img[i], cfg))
            for (int ch = 0; ch < cfg.SYMBOLS_PER_PIXEL; ++ch) syms.push_back(-1);
        else
            for (int ch = 0; ch < cfg.SYMBOLS_PER_PIXEL; ++ch)
                syms.push_back(decodeToSymbol(img[i + ch], cfg));
    }
    {
        std::ostringstream oss;
        oss << "Extracted symbols: " << syms.size() << "\n";
        LOG(oss.str());
    }
    auto bits = symbolsToBinary(syms, bps, totalSyms * bps);
    {
        std::ostringstream oss;
        oss << "Extracted binary: " << bits.size() << " bits\n";
        LOG(oss.str());
    }
    auto data = binaryToData(bits);
    {
        std::ostringstream oss;
        oss << "Extracted raw data: " << data.size() << " bytes\n";
        LOG(oss.str());
    }
    return data;
}

// ---- 提取数据（旧格式兼容：无 header，像素 0 起读，末尾 5 像素跳过）----
std::vector<uint8_t> extractDataLegacyFormat(
    const uint8_t* img, int w, int h, int chs,
    const QRACConfig& cfg, bool& damageOk)
{
    LOG("Trying legacy format (no header)...\n");
    damageOk = checkVerificationPixels(img, w, h, chs, cfg);
    if (!damageOk) LOG("Warning: image damage exceeds threshold.\n");
    int total = w * h, dataPx = total - VERIFICATION_PIXELS;
    int totalSyms = dataPx * cfg.SYMBOLS_PER_PIXEL;
    int intervals = calculateIntervals(cfg), bps = (int)std::log2(intervals);
    std::vector<int> syms; syms.reserve(totalSyms);
    int de = dataPx * chs;
    for (int i = 0; i < de; i += chs) {
        if (isFillerPixel(&img[i], cfg))
            for (int ch = 0; ch < cfg.SYMBOLS_PER_PIXEL; ++ch) syms.push_back(-1);
        else
            for (int ch = 0; ch < cfg.SYMBOLS_PER_PIXEL; ++ch)
                syms.push_back(decodeToSymbol(img[i + ch], cfg));
    }
    auto bits = symbolsToBinary(syms, bps, totalSyms * bps);
    auto data = binaryToData(bits);
    LOG("Legacy format: extracted " << data.size() << " bytes\n");
    return data;
}

// ============================================================
// 工具函数
// ============================================================
std::string toLower(const std::string& s) {
    std::string r = s; std::transform(r.begin(), r.end(), r.begin(), ::tolower); return r;
}
std::string getFileExtension(const std::string& fn) {
    auto p = fn.find_last_of("."); return (p == std::string::npos) ? "" : fn.substr(p + 1);
}
std::string getDirectoryFromPath(const std::string& p) {
    auto x = p.find_last_of("\\/"); return (x == std::string::npos) ? "" : p.substr(0, x + 1);
}
std::string getFilenameWithoutPath(const std::string& p) {
    auto x = p.find_last_of("\\/"); return (x == std::string::npos) ? p : p.substr(x + 1);
}
std::string generateOutputFilename(const std::string& in,
    const std::string& suffix,
    const std::string& ext) {
    std::string d = getDirectoryFromPath(in), n = getFilenameWithoutPath(in);
    auto dot = n.find_last_of("."); if (dot != std::string::npos) n = n.substr(0, dot);
    return d + n + suffix + "." + ext;
}

struct STBImageDeleter { void operator()(unsigned char* p) const { stbi_image_free(p); } };
using STBImagePtr = std::unique_ptr<unsigned char, STBImageDeleter>;

STBImagePtr loadImageSTB(const std::string& fn, int* w, int* h, int* c, int dc = 0) {
#ifdef _WIN32
    auto ws = utf8ToWstring(fn); FILE* f = _wfopen(ws.c_str(), L"rb");
    if (!f) return nullptr;
    auto* d = stbi_load_from_file(f, w, h, c, dc); fclose(f); return STBImagePtr(d);
#else
    return STBImagePtr(stbi_load(fn.c_str(), w, h, c, dc));
#endif
}

STBImagePtr loadImageWithFallback(const std::string& fn, int* w, int* h, int* c) {
    auto img = loadImageSTB(fn, w, h, c, 0);
    if (!img) img = loadImageSTB(fn, w, h, c, 3);
    if (!img) throw QRACException(ErrorType::ImageLoadError, "Failed to load image");
    return img;
}

bool fileExists(const std::string& fn) {
#ifdef _WIN32
    return fs::exists(utf8ToWstring(fn)) && fs::is_regular_file(utf8ToWstring(fn));
#else
    return fs::exists(fn) && fs::is_regular_file(fn);
#endif
}

size_t getFileSize(const std::string& fn) {
    try {
#ifdef _WIN32
        return fs::file_size(utf8ToWstring(fn));
#else
        return fs::file_size(fn);
#endif
    }
    catch (...) {
        throw QRACException(ErrorType::FileReadError, "Cannot get file size: " + fn);
    }
}

bool isJPGFile(const std::string& fn) {
#ifdef _WIN32
    std::ifstream f(utf8ToWstring(fn), std::ios::binary);
#else
    std::ifstream f(fn, std::ios::binary);
#endif
    if (!f.is_open()) return false;
    unsigned char hdr[4];
    f.read(reinterpret_cast<char*>(hdr), 4);
    return hdr[0] == 0xFF && hdr[1] == 0xD8 && hdr[2] == 0xFF
        && (hdr[3] == 0xE0 || hdr[3] == 0xE1);
}

void showJPGWarning() {
    LOG("======================================================\n"
        "                     WARNING: JPG Format\n"
        "======================================================\n"
        "JPG is lossy — not suitable for data encoding.\n"
        "Use PNG or BMP instead.\n"
        "Continue anyway? (y/n): ");
    char ch; std::cin >> ch;
    if (ch != 'y' && ch != 'Y')
        throw QRACException(ErrorType::UserAbort, "Cancelled by user.");
}

std::string detectFileType(const std::vector<uint8_t>& data, const QRACConfig& cfg) {
    if (data.size() < 4) return "bin";
    static const std::unordered_map<std::string, std::vector<uint8_t>> sigs = {
        {"zip",{0x50,0x4B,0x03,0x04}},{"doc",{0xD0,0xCF,0x11,0xE0}},
        {"pdf",{0x25,0x50,0x44,0x46}},{"png",{0x89,0x50,0x4E,0x47}},
        {"jpg",{0xFF,0xD8,0xFF,0xE0}},{"jpg",{0xFF,0xD8,0xFF,0xE1}},
        {"bmp",{0x42,0x4D}},{"gif",{0x47,0x49,0x46,0x38}}
    };
    for (auto& kv : sigs)
        if (data.size() >= kv.second.size()
            && std::equal(kv.second.begin(), kv.second.end(), data.begin()))
            return kv.first;
    size_t n = std::min(data.size(), size_t(1000));
    int pr = 0, ctrl = 0, nul = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = data[i];
        if ((c >= 32 && c <= 126) || c == 9 || c == 10 || c == 13) ++pr;
        else if (c == 0) { ++nul; if (nul > (int)(n / 20)) return "bin"; }
        else if (c < 32) { ++ctrl; if (ctrl > (int)(n / 50)) return "bin"; }
        else ++pr;
    }
    return ((float)pr / n > cfg.TEXT_DETECTION_THRESHOLD
        && (float)ctrl / n < cfg.CONTROL_CHAR_THRESHOLD) ? "txt" : "bin";
}

void saveExtractedData(const std::vector<uint8_t>& data,
    const std::string& fn, bool isText) {
    atomicWriteFile(fn, data.data(), data.size(), !isText);
}

// ---- PNG 压缩 ----
std::vector<uint8_t> compressPNG(const std::vector<uint8_t>& raw,
    int w, int h, int chs) {
    extern int stbi_write_png_compression_level;
    int saved = stbi_write_png_compression_level;
    stbi_write_png_compression_level = 8;
    int outLen = 0;
    unsigned char* png = stbi_write_png_to_mem(raw.data(), w * chs, w, h, chs, &outLen);
    stbi_write_png_compression_level = saved;
    if (!png) throw QRACException(ErrorType::ImageSaveError, "PNG compression failed");
    std::vector<uint8_t> result(png, png + outLen);
    STBIW_FREE(png);
    return result;
}

// ---- 自适应尺寸 ----
void calculateAdaptiveDimensions(size_t dataSize, int* w, int* h,
    const QRACConfig& cfg) {
    size_t tb = dataSize + 4;
    int intervals = calculateIntervals(cfg), bps = (int)std::log2(intervals);
    size_t ts = (tb * 8 + bps - 1) / bps;
    int pn = (int)((ts + cfg.SYMBOLS_PER_PIXEL - 1) / cfg.SYMBOLS_PER_PIXEL)
        + HEADER_PIXELS + VERIFICATION_PIXELS;
    int side = std::max((int)std::ceil(std::sqrt(pn)), cfg.MIN_IMAGE_DIMENSION);
    *w = side; *h = (pn + side - 1) / side;
    *w = std::max(*w, cfg.MIN_IMAGE_DIMENSION);
    *h = std::max(*h, cfg.MIN_IMAGE_DIMENSION);
    LOG("Adaptive dimensions: " << *w << "x" << *h
        << " (pixels needed: " << pn << ")\n");
}

// ---- 通道归一化（decode/correct 共用）----
unsigned char* normalizeChannels(STBImagePtr& imgPtr, int w, int h, int& chs) {
    auto* img = imgPtr.get();
    if (chs >= 3) return img;
    auto* converted = new unsigned char[w * h * 3];
    for (int i = 0; i < w * h; ++i) {
        unsigned char v = (chs == 1) ? img[i] : img[i * chs];
        converted[i * 3 + 0] = v;
        converted[i * 3 + 1] = v;
        converted[i * 3 + 2] = v;
    }
    imgPtr.reset(converted);
    chs = 3;
    return converted;
}

// ============================================================
// 四大主流程
// ============================================================

// ---- 编码 ----
void encodeFile(const QRACConfig& cfg) {
    LOG("[Encode] Select input file...\n");
    std::string inputFile = nativeOpenFileDialog();
    if (inputFile.empty()) {
        LOG("No file selected — cancelled.\n");
        return;
    }
    LOG("Selected: " << inputFile << "\n");

    size_t fileSize = 0;
    try { fileSize = getFileSize(inputFile); }
    catch (...) {}
    if (cfg.largeFileWarningMB > 0 && fileSize > cfg.largeFileWarningMB * 1024 * 1024) {
        LOG("WARNING: File is " << (fileSize / (1024 * 1024)) << " MB.\n"
            << "Encoding may use significant memory. Continue? (y/n): ");
        char ch; std::cin >> ch;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (ch != 'y' && ch != 'Y') {
            LOG("Cancelled.\n");
            return;
        }
    }

    std::string mode, formatChoice;
    LOG("\n=== Encoding Mode ===\n"
        "1. Auto Mode\n"
        "2. Adaptive Mode (minimal image size)\n"
        "Select (1/2, default 1): ");
    std::getline(std::cin, mode);
    if (mode.empty()) mode = "1";

    LOG("\n=== Output Format ===\n"
        "1. PNG (lossless, recommended)\n"
        "2. BMP (24-bit)\n"
        "Select (1/2, default 1): ");
    std::getline(std::cin, formatChoice);
    std::string outFmt = (formatChoice == "2") ? "bmp" : "png";

    std::vector<uint8_t> fileData(fileSize);
#ifdef _WIN32
    std::ifstream f(utf8ToWstring(inputFile), std::ios::binary);
#else
    std::ifstream f(inputFile, std::ios::binary);
#endif
    if (!f.is_open())
        throw QRACException(ErrorType::FileReadError, "Cannot open: " + inputFile);
    f.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    f.close();
    LOG("Read input file: " << fileSize << " bytes\n");

    uint32_t dataSize = static_cast<uint32_t>(fileData.size());
    appendCRC32(fileData);
    LOG("Data + CRC32: " << fileData.size() << " bytes\n");

    int width = 0, height = 0;
    if (mode == "2") {
        calculateAdaptiveDimensions(dataSize, &width, &height, cfg);
    }
    else {
        size_t sz = fileData.size();
        if (sz <= cfg.SMALL_FILE_THRESHOLD) {
            width = height = cfg.DEFAULT_SMALL_SIZE;
        }
        else if (sz <= cfg.MEDIUM_FILE_THRESHOLD) {
            width = height = cfg.DEFAULT_MEDIUM_SIZE;
        }
        else {
            width = height = cfg.DEFAULT_LARGE_SIZE;
        }
    }
    LOG("Image dimensions: " << width << "x" << height << "\n");

    auto bits = dataToBinary(fileData);
    int intervals = calculateIntervals(cfg), bps = (int)std::log2(intervals);
    auto syms = binaryToSymbols(bits, bps, cfg);
    LOG("Symbols: " << syms.size()
        << " (intervals=" << intervals << ", bps=" << bps << ")\n");

    int maxSyms = maxSymbolsInImage(width, height, cfg);
    if ((int)syms.size() > maxSyms) {
        std::ostringstream oss;
        oss << "Capacity exceeded: " << syms.size()
            << " symbols needed, " << maxSyms << " available";
        throw QRACException(ErrorType::ImageSizeError, oss.str());
    }

    auto header = buildHeader(dataSize, cfg);
    auto img = createQRACImage(syms, width, height, cfg, header);

    std::string defaultName = generateOutputFilename(inputFile, "_encoded", outFmt);
    std::string outPath = nativeSaveFileDialog(defaultName.c_str());
    if (outPath.empty()) {
        outPath = defaultName;
        LOG("Using auto-generated filename: " << outPath << "\n");
    }

    if (outFmt == "png") {
        auto png = compressPNG(img, width, height, 3);
        atomicWriteFile(outPath, png.data(), png.size());
    }
    else {
        std::string tmpPath = outPath + ".tmp";
        if (!stbi_write_bmp(tmpPath.c_str(), width, height, 3, img.data()))
            throw QRACException(ErrorType::ImageSaveError, "BMP save failed");
        std::error_code ec;
        fs::rename(tmpPath, outPath, ec);
        if (ec) {
            try { fs::remove(tmpPath); }
            catch (...) {}
            throw QRACException(ErrorType::FileWriteError,
                "Cannot finalize: " + outPath);
        }
    }
    LOG("QRAC image saved: " << outPath << "\n");

    if (cfg.verifyRoundtrip) {
        LOG("Roundtrip verification...\n");
        int vw = 0, vh = 0, vch = 0;
        auto vImg = loadImageWithFallback(outPath, &vw, &vh, &vch);
        auto* vpx = normalizeChannels(vImg, vw, vh, vch);
        DecodedHeader vdh = parseHeader(vpx);
        if (!vdh.valid) {
            LOG("  FAILED: Cannot parse header of encoded image\n");
        }
        else {
            QRACConfig vcfg = configFromHeader(vdh);
            bool vDmg = false;
            auto vRaw = extractRawDataFromImage(vpx, vw, vh, vch, vcfg, vDmg);
            bool vCrc = verifyAndStripCRC32(vRaw);
            if (vCrc && vRaw.size() == dataSize &&
                std::equal(fileData.begin(), fileData.begin() + std::min(dataSize, (uint32_t)vRaw.size()),
                    vRaw.begin())) {
                LOG("  PASSED: Roundtrip verification OK\n");
            }
            else {
                LOG("  FAILED: Roundtrip mismatch (CRC="
                    << (vCrc ? "OK" : "BAD") << ")\n");
            }
        }
    }

    LOG("Encoding complete.\n");
}

// ---- 解码（含旧格式兼容）----
void decodeFile(const QRACConfig& defaultCfg) {
    LOG("[Decode] Select QRAC image...\n");
    std::string inputImage = nativeOpenFileDialog();
    if (inputImage.empty()) {
        LOG("No file selected — cancelled.\n");
        return;
    }
    LOG("Selected: " << inputImage << "\n");

    std::string ext = toLower(getFileExtension(inputImage));
    if (ext == "jpg" || ext == "jpeg") {
        if (isJPGFile(inputImage)) showJPGWarning();
        LOG("JPG decoding is experimental.\n");
    }

    int w = 0, h = 0, chs = 0;
    auto imgPtr = loadImageWithFallback(inputImage, &w, &h, &chs);
    auto* img = normalizeChannels(imgPtr, w, h, chs);
    LOG("Loaded: " << w << "x" << h << " " << chs << "ch\n");

    DecodedHeader dh = parseHeader(img);
    std::vector<uint8_t> raw;
    bool damageOk = false;
    bool crcOk = false;
    uint32_t dataSize;

    if (dh.valid) {
        LOG("Header: dataSize=" << dh.dataSize << " L=" << dh.L
            << " fillerMax=" << (int)dh.fillerMax
            << " version=" << dh.formatVer << "\n");
        QRACConfig imgCfg = configFromHeader(dh);
        raw = extractRawDataFromImage(img, w, h, chs, imgCfg, damageOk);
        LOG("CRC32 verification: ");
        crcOk = verifyAndStripCRC32(raw);
        LOG((crcOk ? "OK" : "FAILED — data may be corrupted!") << "\n");
        dataSize = dh.dataSize;
    }
    else {
        LOG("Header checksum invalid — trying legacy format...\n");
        LOG("Using default L=" << defaultCfg.L
            << " fillerMax=" << (int)defaultCfg.FILLER_MAX_VALUE << "\n");
        LOG("Legacy mode: enter original data size (in bytes) for truncation\n"
            << "(or press Enter to skip truncation): ");
        std::string sizeStr;
        std::getline(std::cin, sizeStr);
        raw = extractDataLegacyFormat(img, w, h, chs, defaultCfg, damageOk);
        crcOk = verifyAndStripCRC32(raw);
        LOG("CRC32: " << (crcOk ? "OK" : "FAILED") << "\n");
        if (!sizeStr.empty()) {
            try { dataSize = (uint32_t)std::stoull(sizeStr); }
            catch (...) { dataSize = (uint32_t)raw.size(); }
        }
        else {
            dataSize = (uint32_t)raw.size();
        }
    }

    if (raw.size() > dataSize) raw.resize(dataSize);

    std::string fileType = detectFileType(raw, defaultCfg);
    LOG("Detected file type: " << fileType << "\n");

    std::string defaultName = generateOutputFilename(inputImage, "_decoded", fileType);
    std::string outPath = nativeSaveFileDialog(defaultName.c_str());
    if (outPath.empty()) {
        outPath = defaultName;
        LOG("Using auto-generated filename: " << outPath << "\n");
    }

    saveExtractedData(raw, outPath, fileType == "txt");
    LOG("Data extracted to: " << outPath << "\n"
        << "Decoding complete.\n");
}

// ---- 校正 ----
void correctImageFile(const QRACConfig& defaultCfg) {
    LOG("[Correct] Select damaged QRAC image...\n");
    std::string inputImage = nativeOpenFileDialog();
    if (inputImage.empty()) {
        LOG("No file selected — cancelled.\n");
        return;
    }
    LOG("Selected: " << inputImage << "\n");

    std::string ext = toLower(getFileExtension(inputImage));
    if (ext == "jpg" || ext == "jpeg")
        throw QRACException(ErrorType::InvalidInput,
            "JPG not supported for correction — use PNG/BMP.");

    int w = 0, h = 0, chs = 0;
    auto imgPtr = loadImageWithFallback(inputImage, &w, &h, &chs);
    auto* img = normalizeChannels(imgPtr, w, h, chs);

    DecodedHeader dh = parseHeader(img);
    std::vector<uint8_t> raw;
    bool damageOk = false;
    bool crcOk = false;
    uint32_t dataSize;
    QRACConfig imgCfg;

    if (dh.valid) {
        LOG("Header: dataSize=" << dh.dataSize << " L=" << dh.L
            << " fillerMax=" << (int)dh.fillerMax
            << " version=" << dh.formatVer << "\n");
        imgCfg = configFromHeader(dh);
        raw = extractRawDataFromImage(img, w, h, chs, imgCfg, damageOk);
        dataSize = dh.dataSize;
    }
    else {
        LOG("Header checksum invalid — trying legacy format...\n");
        imgCfg = defaultCfg;
        LOG("Using default L=" << defaultCfg.L
            << " fillerMax=" << (int)defaultCfg.FILLER_MAX_VALUE << "\n");
        LOG("Enter original data size (bytes) for truncation (or Enter to skip): ");
        std::string sizeStr;
        std::getline(std::cin, sizeStr);
        raw = extractDataLegacyFormat(img, w, h, chs, defaultCfg, damageOk);
        if (!sizeStr.empty()) {
            try { dataSize = (uint32_t)std::stoull(sizeStr); }
            catch (...) { dataSize = (uint32_t)raw.size(); }
        }
        else {
            dataSize = (uint32_t)raw.size();
        }
    }

    LOG("\nStep 1: CRC32 verification... ");
    crcOk = verifyAndStripCRC32(raw);
    LOG((crcOk ? "OK" : "FAILED") << "\n");

    if (!crcOk) {
        LOG("Image data is damaged. Re-encoding may recover via quantisation.\n"
            "Proceed? (y/n): ");
        char ch; std::cin >> ch;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (ch != 'y' && ch != 'Y')
            throw QRACException(ErrorType::UserAbort, "Correction cancelled.");
    }

    if (raw.size() > dataSize) raw.resize(dataSize);
    uint32_t finalSize = static_cast<uint32_t>(raw.size());
    appendCRC32(raw);

    LOG("\nStep 2: Re-encoding with anchor values...\n");
    auto bits = dataToBinary(raw);
    int intervals = calculateIntervals(imgCfg), bps = (int)std::log2(intervals);
    auto syms = binaryToSymbols(bits, bps, imgCfg);

    int maxSyms = maxSymbolsInImage(w, h, imgCfg);
    if ((int)syms.size() > maxSyms) {
        std::ostringstream oss;
        oss << "Cannot correct: " << syms.size() << " symbols needed, "
            << maxSyms << " available in " << w << "x" << h;
        throw QRACException(ErrorType::ImageSizeError, oss.str());
    }

    auto header = buildHeader(finalSize, imgCfg);
    auto newImg = createQRACImage(syms, w, h, imgCfg, header);

    LOG("\nStep 3: Saving corrected image...\n");
    auto png = compressPNG(newImg, w, h, 3);
    std::string defaultName = generateOutputFilename(inputImage, "_corrected", "png");
    std::string outPath = nativeSaveFileDialog(defaultName.c_str());
    if (outPath.empty()) outPath = defaultName;

    atomicWriteFile(outPath, png.data(), png.size());
    LOG("Corrected image saved: " << outPath << "\n");
}

// ---- 批量编码 ----
void batchEncode(const QRACConfig& cfg) {
    LOG("[Batch Encode] Select source folder...\n");
    std::string folder = nativeOpenFolderDialog();
    if (folder.empty()) {
        LOG("No folder selected — cancelled.\n");
        return;
    }
    LOG("Folder: " << folder << "\n");

    std::vector<std::string> files;
    try {
        for (const auto& entry : fs::directory_iterator(
#ifdef _WIN32
            utf8ToWstring(folder)
#else
            folder
#endif
        )) {
            if (entry.is_regular_file()) {
#ifdef _WIN32
                std::wstring wp = entry.path().wstring();
                int len = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string sp(len, 0);
                WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, &sp[0], len, nullptr, nullptr);
                files.push_back(sp);
#else
                files.push_back(entry.path().string());
#endif
            }
        }
    }
    catch (...) {
        throw QRACException(ErrorType::FileReadError, "Cannot read folder: " + folder);
    }

    if (files.empty()) {
        LOG("No files found.\n");
        return;
    }

    LOG("Found " << files.size() << " file(s)\n");

    LOG("\n=== Output Format ===\n"
        "1. PNG (lossless, recommended)\n"
        "2. BMP (24-bit)\n"
        "Select (1/2, default 1): ");
    std::string formatChoice;
    std::getline(std::cin, formatChoice);
    std::string outFmt = (formatChoice == "2") ? "bmp" : "png";

    int success = 0, failed = 0;
    for (size_t idx = 0; idx < files.size(); ++idx) {
        const auto& fp = files[idx];
        LOG("\n[" << (idx + 1) << "/" << files.size() << "] "
            << getFilenameWithoutPath(fp) << "\n");

        try {
            size_t fs = getFileSize(fp);
            if (cfg.largeFileWarningMB > 0 && fs > cfg.largeFileWarningMB * 1024 * 1024) {
                LOG("  SKIPPED: File too large (" << (fs / (1024 * 1024)) << " MB)\n");
                ++failed;
                continue;
            }

            std::vector<uint8_t> fileData(fs);
#ifdef _WIN32
            std::ifstream f(utf8ToWstring(fp), std::ios::binary);
#else
            std::ifstream f(fp, std::ios::binary);
#endif
            if (!f.is_open()) { LOG("  SKIPPED: Cannot open\n"); ++failed; continue; }
            f.read(reinterpret_cast<char*>(fileData.data()), fs);
            f.close();

            uint32_t dataSize = (uint32_t)fileData.size();
            appendCRC32(fileData);

            int w = 0, h = 0;
            calculateAdaptiveDimensions(dataSize, &w, &h, cfg);

            auto bits = dataToBinary(fileData);
            int intervals = calculateIntervals(cfg), bps = (int)std::log2(intervals);
            auto syms = binaryToSymbols(bits, bps, cfg);

            int maxSyms = maxSymbolsInImage(w, h, cfg);
            if ((int)syms.size() > maxSyms) {
                LOG("  SKIPPED: capacity exceeded\n"); ++failed; continue;
            }

            auto header = buildHeader(dataSize, cfg);
            auto img = createQRACImage(syms, w, h, cfg, header);
            std::string outPath = generateOutputFilename(fp, "_encoded", outFmt);

            if (outFmt == "png") {
                auto png = compressPNG(img, w, h, 3);
                atomicWriteFile(outPath, png.data(), png.size());
            }
            else {
                std::string tmpPath = outPath + ".tmp";
                if (!stbi_write_bmp(tmpPath.c_str(), w, h, 3, img.data())) {
                    LOG("  SKIPPED: BMP write failed\n"); ++failed; continue;
                }
                std::error_code ec;
                fs::rename(tmpPath, outPath, ec);
                if (ec) { try { fs::remove(tmpPath); } catch (...) {} ++failed; continue; }
            }
            LOG("  -> " << getFilenameWithoutPath(outPath) << "\n");
            ++success;
        }
        catch (const std::exception& e) {
            LOG("  FAILED: " << e.what() << "\n");
            ++failed;
        }
    }
    LOG("\nBatch complete: " << success << " success, " << failed << " failed\n");
}

// ============================================================
// UI
// ============================================================
void showUserGuide() {
    LOG("======================================================\n"
        "               QRAC Tool Suite User Guide\n"
        "======================================================\n"
        "1. Encode     — convert any file to a QRAC image (PNG/BMP)\n"
        "2. Decode     — extract original file from a QRAC image\n"
        "3. Correct    — repair damaged QRAC images via re-anchoring\n"
        "4. Batch      — encode all files in a folder\n"
        "5. Settings   — view/edit configuration\n"
        "6. User Guide\n"
        "7. Trust Statement\n"
        "8. Exit\n"
        "======================================================\n\n");
}

void showTrustStatement() {
    LOG("=== Trust Statement ===\n"
        "QRAC is fully open-source, no malware.\n"
        "Source: https://github.com/sans666VIP/QRAC\n"
        "========================\n\n");
}

void showSettings(QRACConfig& cfg) {
    while (true) {
        LOG("\n--- Current Settings ---\n"
            << "1. L (quantization interval)     = " << cfg.L << "\n"
            << "2. FILLER_MAX_VALUE               = " << (int)cfg.FILLER_MAX_VALUE << "\n"
            << "3. DAMAGE_THRESHOLD               = " << (int)cfg.DAMAGE_THRESHOLD << "\n"
            << "4. FEC_REDUNDANCY_RATIO           = " << cfg.FEC_REDUNDANCY_RATIO << "\n"
            << "5. MIN_IMAGE_DIMENSION            = " << cfg.MIN_IMAGE_DIMENSION << "\n"
            << "6. DEFAULT_SMALL_SIZE             = " << cfg.DEFAULT_SMALL_SIZE << "\n"
            << "7. DEFAULT_MEDIUM_SIZE            = " << cfg.DEFAULT_MEDIUM_SIZE << "\n"
            << "8. DEFAULT_LARGE_SIZE             = " << cfg.DEFAULT_LARGE_SIZE << "\n"
            << "9. SMALL_FILE_THRESHOLD           = " << cfg.SMALL_FILE_THRESHOLD << "\n"
            << "10. MEDIUM_FILE_THRESHOLD         = " << cfg.MEDIUM_FILE_THRESHOLD << "\n"
            << "11. Roundtrip verification        = " << (cfg.verifyRoundtrip ? "ON" : "OFF") << "\n"
            << "12. Large file warning threshold  = " << cfg.largeFileWarningMB << " MB\n"
            << "13. Save & Return\n"
            << "14. Return without saving\n"
            << "Select: ");

        int c = 0;
        std::cin >> c;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (c == 13) { saveConfig(cfg); LOG("Config saved.\n"); return; }
        if (c == 14) return;
        if (c < 1 || c > 12) continue;

        LOG("New value: ");
        std::string val;
        std::getline(std::cin, val);
        if (val.empty()) continue;
        try {
            switch (c) {
            case 1:  cfg.L = std::max(1, std::stoi(val)); break;
            case 2:  cfg.FILLER_MAX_VALUE = (uint8_t)std::clamp(std::stoi(val), 0, 200); break;
            case 3:  cfg.DAMAGE_THRESHOLD = (uint8_t)std::clamp(std::stoi(val), 1, 50); break;
            case 4:  cfg.FEC_REDUNDANCY_RATIO = std::clamp(std::stof(val), 0.0f, 1.0f); break;
            case 5:  cfg.MIN_IMAGE_DIMENSION = std::max(4, std::stoi(val)); break;
            case 6:  cfg.DEFAULT_SMALL_SIZE = std::max(16, std::stoi(val)); break;
            case 7:  cfg.DEFAULT_MEDIUM_SIZE = std::max(16, std::stoi(val)); break;
            case 8:  cfg.DEFAULT_LARGE_SIZE = std::max(16, std::stoi(val)); break;
            case 9:  cfg.SMALL_FILE_THRESHOLD = std::max(size_t(1), std::stoull(val)); break;
            case 10: cfg.MEDIUM_FILE_THRESHOLD = std::max(size_t(1), std::stoull(val)); break;
            case 11: cfg.verifyRoundtrip = (std::stoi(val) != 0); break;
            case 12: cfg.largeFileWarningMB = std::max(size_t(0), std::stoull(val)); break;
            }
        }
        catch (...) { LOG("Invalid value.\n"); }
    }
}

void showMenu() {
    QRACConfig cfg;
    loadConfig(cfg);

    showUserGuide();
    showTrustStatement();

    while (true) {
        LOG("\n======================================================\n"
            "              QRAC Integrated Tool Suite  v"
            << QRAC_SOFTWARE_VER << "\n"
            "======================================================\n"
            "1. Encode        2. Decode        3. Correct\n"
            "4. Batch Encode  5. Settings\n"
            "6. User Guide    7. Trust Statement    8. Exit\n"
            "Select (1-8): ");
        int choice = 0;
        std::cin >> choice;
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            LOG("Invalid input.\n");
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        try {
            switch (choice) {
            case 1: encodeFile(cfg);       break;
            case 2: decodeFile(cfg);       break;
            case 3: correctImageFile(cfg); break;
            case 4: batchEncode(cfg);      break;
            case 5: showSettings(cfg);     break;
            case 6: showUserGuide();       break;
            case 7: showTrustStatement();  break;
            case 8:
                saveConfig(cfg);
                LOG("Exiting.\n");
                return;
            default:
                LOG("Please select 1-8.\n");
            }
        }
        catch (const QRACException& e) {
            std::cerr << "Error: " << e.what() << "\n";
            LOG("Error: " << e.what() << "\n");
            switch (e.getType()) {
            case ErrorType::FileNotFound:
                LOG("Check the file path.\n"); break;
            case ErrorType::FileReadError:
                LOG("Ensure the file is accessible.\n"); break;
            case ErrorType::FileWriteError:
                LOG("Check write permissions.\n"); break;
            case ErrorType::ImageLoadError:
                LOG("Image may be corrupt or unsupported.\n"); break;
            case ErrorType::UserAbort:
                LOG("Cancelled.\n"); break;
            default: break;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << "\n";
            LOG("Unexpected error: " << e.what() << "\n");
        }
    }
}

int main() {
    initLogFile();
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    LOG("--- QRAC v" << QRAC_SOFTWARE_VER << " session: " << std::ctime(&t));

    std::cout << "QRAC Tool Suite - v" << QRAC_SOFTWARE_VER << "\n"
        << "Format v" << QRAC_FORMAT_VERSION
        << " | CRC32 + Header checksum + Atomic writes\n"
        << "Single-file build, no extra dependencies\n\n";

    try {
        showMenu();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\nPress Enter to exit...";
        LOG("Fatal: " << e.what() << "\n");
        std::cin.get();
        closeLogFile();
        return 1;
    }
    closeLogFile();
    return 0;
}
