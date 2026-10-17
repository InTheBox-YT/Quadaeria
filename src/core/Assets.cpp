#include "core/Assets.h"
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

namespace qc {
namespace fs = std::filesystem;

// env var first, then next to the exe, then the source folder as a last resort
static fs::path findRoot() {
    if (const char* env = std::getenv("QUADAERIA_ASSETS"); env && *env) return env;
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    std::vector<fs::path> tries;
    if (!ec) {
        tries.push_back(exe.parent_path() / "assets");
        tries.push_back(exe.parent_path().parent_path() / "assets");
    }
    tries.push_back(fs::current_path(ec) / "assets");
#ifdef QC_ASSET_DIR
    tries.push_back(QC_ASSET_DIR);
#endif
    for (const fs::path& p : tries)
        if (fs::is_directory(p / "textures", ec)) return p;
    return "assets";
}

std::string assetPath(const std::string& relative) {
    static const fs::path root = findRoot();
    return (root / relative).string();
}

}
