#pragma once
// Stage-B source root substrate. The Forge parser still rejects imports;
// successful resolution does NOT imply module graph or executable import parity.
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::imports {
namespace fs = std::filesystem;

inline bool within(const fs::path& root, const fs::path& path) {
    auto r = root.begin();
    auto p = path.begin();
    for (; r != root.end(); ++r, ++p)
        if (p == path.end() || *r != *p) return false;
    return true;
}

// Requires an explicitly selected, physically verified package manifest.
// Never falls back to the current directory, vendor, or ambient search paths.
inline fs::path project_root_source(const fs::path& manifest_dir,
                                    std::string_view specifier) {
    if (specifier.size() < 3 || specifier.substr(0, 2) != "+/")
        throw std::invalid_argument("expected canonical +/ source import");
    if (specifier.size() > 4096)
        throw std::invalid_argument("rooted import exceeds 4096 bytes");
    const auto suffix = specifier.substr(2);
    if (suffix.empty())
        throw std::invalid_argument("empty rooted import");
    for (unsigned char c : suffix)
        if (c < 32 || c == 127 || c == '\\' || c == ':')
            throw std::invalid_argument("invalid rooted import byte or separator");
    std::size_t start = 0;
    while (true) {
        const auto slash = suffix.find('/', start);
        const auto part = suffix.substr(start, slash == std::string_view::npos
            ? suffix.size() - start : slash - start);
        if (part.empty() || part == "." || part == "..")
            throw std::invalid_argument("noncanonical rooted import component");
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    const auto root = fs::canonical(manifest_dir);
    const auto manifest = root / ".ores.pkg.toml";
    if (!fs::is_regular_file(manifest) ||
        !within(root, fs::canonical(manifest)))
        throw std::invalid_argument("verified package manifest required");
    auto candidate = root / fs::path(std::string(suffix));
    if (candidate.extension().empty()) candidate += ".ores";
    if (!fs::is_regular_file(candidate))
        throw std::invalid_argument("project-root source not found");
    if (candidate.extension() != ".ores")
        throw std::invalid_argument("non-Oreslang source requires unsupported backend");
    const auto physical = fs::canonical(candidate);
    if (physical.extension() != ".ores" || !within(root, physical))
        throw std::invalid_argument("project-root source escapes physical package root");
    return physical;
}
} // namespace oresforge::imports
