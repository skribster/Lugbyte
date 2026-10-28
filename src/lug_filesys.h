#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Luggage::Filesystem {

std::optional<std::filesystem::path> ResolveModulePath(const std::filesystem::path& currentModule, const std::filesystem::path& requestedModule);
std::optional<std::string> ReadModule(const std::filesystem::path& module);

}