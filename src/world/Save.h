#pragma once
#include "world/World.h"
#include <memory>
#include <string>
#include <vector>

namespace qc {
std::string columnPath(const std::string& worldDir, int cx, int cz);
bool saveColumn(const std::string& worldDir, const Column& c);
std::unique_ptr<Column> loadColumn(const std::string& worldDir, int cx, int cz);

struct KeyValues {
    std::vector<std::pair<std::string, std::string>> items;
    std::string get(const std::string& k, const std::string& def = "") const;
    double num(const std::string& k, double def = 0) const;
    void set(const std::string& k, const std::string& v);
    void set(const std::string& k, double v);
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

std::string dataDir();
bool makeDirs(const std::string& path);
std::vector<std::string> listDirs(const std::string& path);
bool removeTree(const std::string& path);
bool fileExists(const std::string& path);

}
