#pragma once
// Stage-B source root substrate. The Forge parser still rejects imports;
// successful resolution does NOT imply module graph or executable import parity.
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
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
// TOML permits LF and CRLF only. A bare CR, including one hidden by a
// trailing # comment, must not let an unparsed [project] or root declaration
// silently inherit the default package-root authority.
inline void reject_bare_manifest_cr(std::string& line, bool eof_without_lf) {
    const auto cr = line.find('\r');
    if (cr == std::string::npos) return;
    if (cr != line.size() - 1 || eof_without_lf)
        throw std::invalid_argument("bare carriage return in project manifest");
    line.pop_back();
}

// Read through a fixed-size buffer before any line-based parsing. getline
// otherwise allocates an attacker-controlled line before checking our 64 KiB
// manifest budget. EOF is not a substitute for a byte-bounded snapshot.
inline std::string read_bounded_manifest_text(const fs::path& manifest) {
    std::ifstream stream(manifest, std::ios::binary);
    if (!stream) throw std::invalid_argument("cannot open verified package manifest");
    std::string bytes(65537, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto count = stream.gcount();
    if (stream.bad() || count < 0)
        throw std::invalid_argument("cannot read verified package manifest");
    if (count > 65536)
        throw std::invalid_argument("package manifest exceeds 64 KiB");
    bytes.resize(static_cast<std::size_t>(count));
    return bytes;
}

inline void require_v1_manifest(const fs::path& manifest) {
    std::istringstream input{read_bounded_manifest_text(manifest)};
    const std::regex version(R"ores(^[ \t]*schema_version[ \t]*=[ \t]*"1"[ \t]*(?:#.*)?$)ores");
    const std::regex key(R"ores(^[ \t]*(?:schema_version|"schema_version"|'schema_version')[ \t]*=.*$)ores");
    std::string line;
    std::size_t consumed = 0, count = 0;
    bool top_level = true;
    while (std::getline(input, line)) {
        // Count the delimiter only when getline consumed one. A legal final
        // line without LF must not be counted as one byte larger than stored.
        const auto line_bytes = line.size() + (input.eof() ? 0U : 1U);
        if (line_bytes > 65536 - consumed)
            throw std::invalid_argument("package manifest exceeds 64 KiB");
        consumed += line_bytes;
        reject_bare_manifest_cr(line, input.eof());
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

// Conservative subset of TOML v1: respect [project].root rather than
// silently resolving +/ relative to the manifest when an explicit root
// exists. Reject unsupported root forms; this is not a full TOML parser.
inline fs::path configured_project_root(const fs::path& manifest_dir,
                                        const fs::path& manifest) {
    std::istringstream input{read_bounded_manifest_text(manifest)};
    const std::regex project_table(R"ores(^[ \t]*\[project\][ \t]*(?:#.*)?$)ores");
    const std::regex section(R"ores(^[ \t]*\[.*$)ores");
    const std::regex root_key(R"ores(^[ \t]*(?:root|"root"|'root')[ \t]*=.*$)ores");
    const std::regex root_value(R"ores(^[ \t]*root[ \t]*=[ \t]*"([^"]+)"[ \t]*(?:#.*)?$)ores");
    const std::regex dotted_root(R"ores(^[ \t]*project[ \t]*\.[ \t]*root[ \t]*=.*$)ores");
    std::string line, root_text = ".";
    bool in_project = false, seen_project_table = false, found_root = false;
    while (std::getline(input, line)) {
        reject_bare_manifest_cr(line, input.eof());
        if (std::regex_match(line, section)) {
            in_project = std::regex_match(line, project_table);
            if (!in_project && line.find("project") != std::string::npos)
                throw std::invalid_argument("unsupported project manifest table form");
            if (in_project && seen_project_table)
                throw std::invalid_argument("duplicate project table");
            if (in_project) seen_project_table = true;
            continue;
        }
        if (std::regex_match(line, dotted_root))
            throw std::invalid_argument("unsupported dotted project.root form");
        if (in_project && std::regex_match(line, root_key)) {
            if (found_root) throw std::invalid_argument("duplicate project.root");
            std::smatch match;
            if (!std::regex_match(line, match, root_value))
                throw std::invalid_argument("unsupported project.root value");
            root_text = match[1].str();
            found_root = true;
        }
    }
    if (!input.eof()) throw std::invalid_argument("unreadable package project.root");
    if (root_text.size() > 1024 || root_text.empty() ||
        root_text.find('\\') != std::string::npos ||
        root_text.find(':') != std::string::npos ||
        (root_text != "." &&
         (root_text.front() == '/' || root_text.back() == '/' ||
          root_text.find("//") != std::string::npos)))
        throw std::invalid_argument("invalid project.root path");
    for (unsigned char byte : root_text)
        if (byte < 32 || byte == 127)
            throw std::invalid_argument("control byte in project.root");
    const fs::path relative(root_text);
    if (relative.is_absolute())
        throw std::invalid_argument("absolute project.root unsupported in Forge bootstrap");
    auto inspected = manifest_dir;
    for (const auto& part : relative) {
        if (part == ".." || part.empty() || (part == "." && root_text != "."))
            throw std::invalid_argument("project.root escapes manifest directory");
        if (part == ".") continue;
        inspected /= part;
        for (const auto* name : {".ores.pkg.toml", ".oreslangc.cfg.toml"}) {
            if (fs::symlink_status(inspected / name).type() != fs::file_type::not_found)
                throw std::invalid_argument("project.root crosses nested project manifest");
        }
    }
    std::error_code root_error;
    const auto selected = fs::canonical(manifest_dir / relative, root_error);
    if (root_error || !fs::is_directory(selected))
        throw std::invalid_argument("project.root is not an existing directory");
    if (!within(manifest_dir, selected))
        throw std::invalid_argument("project.root escapes physical manifest directory");
    return selected;
}

inline void require_no_nested_manifest(const fs::path& root, const fs::path& candidate) {
    for (auto parent = candidate.parent_path(); parent != root;
         parent = parent.parent_path()) {
        if (parent.empty() || parent == parent.root_path() || !within(root, parent))
            throw std::invalid_argument("project-root source escapes physical package root");
        for (const auto* name : {".ores.pkg.toml", ".oreslangc.cfg.toml"})
            if (fs::symlink_status(parent / name).type()
                    != fs::file_type::not_found)
                throw std::invalid_argument("source import crosses nested package boundary");
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
    // A symlink marker can inherit different package-root authority even
    // when its target happens to be inside this directory. A real modern
    // manifest file is mandatory, never a legacy-manifest alias.
    if (fs::is_symlink(manifest) || !fs::is_regular_file(manifest) ||
        fs::canonical(manifest).parent_path() != root)
        throw std::invalid_argument("verified package manifest must be in root directory");
    require_v1_manifest(manifest);
    const auto selected_root = configured_project_root(root, manifest);
    auto candidate = selected_root / fs::path(std::string(suffix));
    if (candidate.extension().empty()) candidate += ".ores";
    if (!fs::is_regular_file(candidate))
        throw std::invalid_argument("project-root source not found");
    if (candidate.extension() != ".ores")
        throw std::invalid_argument("non-Oreslang source requires unsupported backend");
    const auto physical = fs::canonical(candidate);
    if (physical.extension() != ".ores" || !within(selected_root, physical))
        throw std::invalid_argument("project-root source escapes physical package root");
    // A source spelled through a nested package symlink to a parent-owned
    // physical file is still a cross-package import and must be denied.
    require_no_nested_manifest(selected_root, candidate);
    require_no_nested_manifest(selected_root, physical);
    return physical;
}
// Host-only explicit relative resolution: this does not wire any import
// into Forge's parser/IR. The caller must supply the already-authorized
// physical package source root. Do not infer authority from process CWD.
inline fs::path relative_source(const fs::path& importer,
                               std::string_view specifier,
                               const fs::path& source_root) {
    if (!(specifier.starts_with("./") || specifier.starts_with("../")) ||
        specifier.size() > 4096)
        throw std::invalid_argument("expected explicit ./ or ../ source import");
    for (unsigned char c : specifier)
        if (c < 32 || c == 127 || c == '\\' || c == ':')
            throw std::invalid_argument("invalid relative source separator");
    if (specifier.find("//") != std::string_view::npos ||
        specifier.back() == '/')
        throw std::invalid_argument("invalid relative source path segment");
    const fs::path root = fs::canonical(source_root);
    // Check both spelled and physical importer paths. Canonicalizing first
    // alone would erase traversal into a nested package via a back-reference.
    const fs::path spelled_owner = fs::absolute(importer).lexically_normal();
    const fs::path owner = fs::canonical(importer);
    if (!fs::is_regular_file(owner) || owner.extension() != ".ores" ||
        !within(root, owner) || !within(root, spelled_owner))
        throw std::invalid_argument("relative importer is outside source root");
    require_no_nested_manifest(root, spelled_owner);
    require_no_nested_manifest(root, owner);
    auto spelled = (owner.parent_path() / fs::path(std::string(specifier))).lexically_normal();
    if (spelled.extension().empty()) spelled += ".ores";
    if (spelled.extension() != ".ores" || !within(root, spelled))
        throw std::invalid_argument("relative source escapes owning package");
    if (!fs::is_regular_file(spelled))
        throw std::invalid_argument("relative source file not found");
    const auto physical = fs::canonical(spelled);
    if (physical.extension() != ".ores" || !within(root, physical))
        throw std::invalid_argument("relative source escapes physical package root");
    if (physical == owner)
        throw std::invalid_argument("relative source cannot import itself");
    require_no_nested_manifest(root, spelled);
    require_no_nested_manifest(root, physical);
    return physical;
}

} // namespace oresforge::imports
