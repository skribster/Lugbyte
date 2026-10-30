#include "lug_filesys.h"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Luggage::Filesystem {

std::optional<fs::path> ResolveModulePath(const fs::path& importerPath, const fs::path& requestedPath) {
    fs::path candidate = requestedPath;

    /*
        If the passed-in path is relative, we shall resolve it against the
        requiring script's current directory. e.g.

        directory/
            a.lug
            b.lug

        if a.lug has:
        box b <- unbox("b.lug")

        we want to search inside the parent path of a.lug, which is directory/.
    */
    if (candidate.is_relative()) {
        // Empty script path means REPL (or string eval): fall back to current
        // working directory
        fs::path base = importerPath.empty()
            ? fs::current_path()
            : importerPath.parent_path();
        candidate = base / candidate;
    }

    // Allow users to omit the extension, e.g. unbox("a")
    if (!candidate.has_extension()) {
        candidate.replace_extension(".lug");
    }

    std::error_code errorCode;
    // Convert the candidate into an absolute path
    fs::path canonical = fs::weakly_canonical(candidate, errorCode);
    if (errorCode || !fs::is_regular_file(canonical)) {
        return std::nullopt;
    }

    return canonical;
}


std::optional<std::string> ReadModule(const fs::path& module) {
    std::ifstream file(module, std::ios::binary);
    if (!file) return std::nullopt;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

}