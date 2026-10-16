#pragma once
// Stage-B source root substrate. The Forge parser still rejects imports;
// successful resolution does NOT imply module graph or executable import parity.
#include <filesystem>
#include <fstream>
#include <regex>
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

// Restricted v1 bootstrap admission, NOT complete TOML support. Fail closed
// when the schema marker is missing, duplicated, contradictory, or concealed
// inside a multiline literal. Full manifest semantics require a TOML parser.
inline void require_v1_manifest(const fs::path& manifest) {
    std::ifstream input(manifest, std::ios::binary);
    if (!input) throw std::invalid_argument("cannot open verified package manifest");
    const std::regex version(R"ores(^[ \t]*schema_version[ \t]*=[ \t]*"1"[ \t]*(?:#.*)?$)ores");
    const std::regex key(R"ores(^[ \t]*(?:schema_version|"schema_version"|'schema_version')[ \t]*=.*$)ores");
    std::string line;
    std::size_t consumed = 0, count = 0;
    bool top_level = true;
    while (std::getline(input, line)) {
        if (line.size() > 65536 || consumed > 65536 - line.size())
            throw std::invalid_argument("package manifest exceeds 64 KiB");
        consumed += line.size();
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find('\0') != std::string::npos ||
            line.find("\"\"\"") != std::string::npos ||
            line.find("'''") != std::string::npos)
            throw std::invalid_argument("unsupported package manifest literal");
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '[') top_level = false;
        if (top_level && std::regex_match(line, key)) {
            if (!std::regex_match(line, version))
                throw std::invalid_argument("package manifest requires schema_version = 1");
            ++count;
        }
    }
    if (!input.eof() || count != 1)
        throw std::invalid_argument("package manifest requires exactly one schema_version = 1");
}

inline void require_no_nested_manifest(const fs::path& root, const fs::path& candidate) {
    for (auto parent = candidate.parent_path(); parent != root;
         parent = parent.parent_path()) {
        if (parent.empty() || parent == parent.root_path() || !within(root, parent))
            throw std::invalid_argument("project-root source escapes physical package root");
        if (fs::symlink_status(parent / ".ores.pkg.toml").type()
                != fs::file_type::not_found)
            throw std::invalid_argument("root import crosses nested package boundary");
    }
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
        fs::canonical(manifest).parent_path() != root)
        throw std::invalid_argument("verified package manifest must be in root directory");
    require_v1_manifest(manifest);
    auto candidate = root / fs::path(std::string(suffix));
    if (candidate.extension().empty()) candidate += ".ores";
    if (!fs::is_regular_file(candidate))
        throw std::invalid_argument("project-root source not found");
    if (candidate.extension() != ".ores")
        throw std::invalid_argument("non-Oreslang source requires unsupported backend");
    const auto physical = fs::canonical(candidate);
    if (physical.extension() != ".ores" || !within(root, physical))
        throw std::invalid_argument("project-root source escapes physical package root");
    // A source spelled through a nested package symlink to a parent-owned
    // physical file is still a cross-package import and must be denied.
    require_no_nested_manifest(root, candidate);
    require_no_nested_manifest(root, physical);
    return physical;
}
} // namespace oresforge::imports
