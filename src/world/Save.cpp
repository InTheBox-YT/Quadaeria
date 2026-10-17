#include <sys/stat.h>
#include "world/Save.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace qc {
std::string columnPath(const std::string& worldDir, int cx, int cz) {
    return worldDir + "/region/c." + std::to_string(cx) + "." + std::to_string(cz) + ".qcc";
}

namespace {
void rle(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    size_t i = 0;
    while (i < in.size()) {
        uint8_t v = in[i];
        size_t run = 1;
        while (i + run < in.size() && in[i + run] == v && run < 255) ++run;
        out.push_back(uint8_t(run));
        out.push_back(v);
        i += run;
    }
}
bool unrle(const uint8_t* p, size_t n, std::vector<uint8_t>& out, size_t expect) {
    out.clear();
    out.reserve(expect);
    for (size_t i = 0; i + 1 < n; i += 2) out.insert(out.end(), p[i], p[i + 1]);
    return out.size() == expect;
}
}

// blocks then meta, rle packed. goes to a .tmp first so a crash mid save doesnt wreck the world
bool saveColumn(const std::string& worldDir, const Column& c) {
    std::vector<uint8_t> raw(size_t(CS3) * NSEC * 2);
    for (int s = 0; s < NSEC; ++s)
        for (int i = 0; i < CS3; ++i) {
            raw[size_t(s) * CS3 + i] = c.sec[s].block(i);
            raw[size_t(NSEC) * CS3 + size_t(s) * CS3 + i] = c.sec[s].metaAt(i);
        }
    std::vector<uint8_t> enc;
    enc.reserve(65536);
    rle(raw, enc);
    makeDirs(worldDir + "/region");
    std::string path = columnPath(worldDir, c.cx, c.cz);
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const char magic[4] = {'Q', 'C', 'C', '3'};
    uint32_t size = uint32_t(enc.size());
    bool ok =
        std::fwrite(magic, 1, 4, f) == 4 && std::fwrite(&size, 4, 1, f) == 1 && std::fwrite(enc.data(), 1, enc.size(), f) == enc.size();
    std::fclose(f);
    if (!ok) return false;
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

std::unique_ptr<Column> loadColumn(const std::string& worldDir, int cx, int cz) {
    if (worldDir.empty()) return nullptr;
    std::string path = columnPath(worldDir, cx, cz);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    char magic[4];
    uint32_t size = 0;
    std::unique_ptr<Column> col;
    if (std::fread(magic, 1, 4, f) == 4 &&
        (std::memcmp(magic, "QCC1", 4) == 0 || std::memcmp(magic, "QCC2", 4) == 0 || std::memcmp(magic, "QCC3", 4) == 0) &&
        std::fread(&size, 4, 1, f) == 1 && size < (64u << 20)) {
        const bool legacyFluid = std::memcmp(magic, "QCC1", 4) == 0;
        const bool waterOnlySourceEra = std::memcmp(magic, "QCC2", 4) == 0;
        std::vector<uint8_t> enc(size), raw;
        if (std::fread(enc.data(), 1, size, f) == size && unrle(enc.data(), enc.size(), raw, size_t(CS3) * NSEC * 2)) {
            col = std::make_unique<Column>();
            col->cx = cx;
            col->cz = cz;
            for (int s = 0; s < NSEC; ++s) {
                Section& sec = col->sec[s];
                sec.blocks.reset(new uint8_t[CS3]);
                sec.meta.reset(new uint8_t[CS3]);
                std::memcpy(sec.blocks.get(), &raw[size_t(s) * CS3], CS3);
                std::memcpy(sec.meta.get(), &raw[size_t(NSEC) * CS3 + size_t(s) * CS3], CS3);
                sec.compact();
            }
            if (legacyFluid || waterOnlySourceEra) {
                for (int z = 0; z < CS; ++z)
                    for (int x = 0; x < CS; ++x)
                        for (int y = 0; y < WH; ++y) {
                            uint8_t b = col->get(x, y, z);
                            if (b != B_WATER && b != B_LAVA) continue;
                            auto& sec = col->sec[y >> 5];
                            int ii = idx3(x, y & 31, z);
                            bool exposed = y + 1 >= WH || col->get(x, y + 1, z) != b;
                            if (legacyFluid)
                                sec.setMeta(ii, exposed ? FLUID_SOURCE_META : FLUID_FULL_META);
                            else if (b == B_LAVA && sec.metaAt(ii) == FLUID_FULL_META && exposed)
                                sec.setMeta(ii, FLUID_SOURCE_META);
                        }
            }
            col->state = ColState::Generated;
            col->modified = true;
        }
    }
    std::fclose(f);
    if (col) col->indexFeatures();
    return col;
}

std::string KeyValues::get(const std::string& k, const std::string& def) const {
    for (auto& [a, b] : items)
        if (a == k) return b;
    return def;
}
double KeyValues::num(const std::string& k, double def) const {
    std::string v = get(k);
    if (v.empty()) return def;
    return std::atof(v.c_str());
}
void KeyValues::set(const std::string& k, const std::string& v) {
    for (auto& [a, b] : items)
        if (a == k) {
            b = v;
            return;
        }
    items.push_back({k, v});
}
void KeyValues::set(const std::string& k, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    set(k, std::string(buf));
}
bool KeyValues::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        set(line.substr(0, eq), line.substr(eq + 1));
    }
    return true;
}
bool KeyValues::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (auto& [a, b] : items) out << a << '=' << b << '\n';
    return bool(out);
}

std::string dataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base = xdg && *xdg ? xdg : (std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.local/share");
    std::string fresh = base + "/quadaeria", old = base + "/quadcraft";
    struct stat st;
    if (::stat(fresh.c_str(), &st) != 0 && ::stat(old.c_str(), &st) == 0) return old;
    return fresh;
}
bool makeDirs(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}
std::vector<std::string> listDirs(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(path, ec))
        if (e.is_directory()) out.push_back(e.path().filename().string());
    return out;
}
bool removeTree(const std::string& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    return !ec;
}
bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

}
