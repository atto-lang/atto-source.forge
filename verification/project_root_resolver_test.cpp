#include "compiler/project_root_resolver.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oresforge::imports;
static void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
static void reject(const std::function<void()>& fn, const char* why) {
    try { fn(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(why);
}
static void write(const fs::path& file, const char* text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file);
    if (!out) throw std::runtime_error("cannot create fixture");
    out << text;
    if (!out) throw std::runtime_error("cannot write fixture");
}
int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto tmp = fs::temp_directory_path() /
        ("oresforge-root-contract-" + std::to_string(stamp));
    fs::create_directories(tmp);
    struct Cleanup {
        fs::path root;
        ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); }
    } cleanup{tmp};
    try {
        const auto project = tmp / "app";
        write(project / "src" / "lib" / "answer.ores", "fnc answer():int{return 42;}\n");
        write(tmp / "other" / "answer.ores", "fnc answer():int{return 0;}\n");
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "missing manifest gained package-root authority");
        write(project / ".ores.pkg.toml", "schema_version = \"2\"\n");
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "unsupported manifest schema admitted");
        write(project / ".ores.pkg.toml", "schema_version = \"1\"\nschema_version = \"1\"\n");
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "duplicate manifest schema admitted");
        write(project / ".ores.pkg.toml", "name = \"test\"\n");
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "missing manifest schema admitted");
        // Blank lines count toward the 64 KiB manifest byte budget.
        const auto oversized = std::string("schema_version = \"1\"\n") +
                               std::string(65536, '\n');
        write(project / ".ores.pkg.toml", oversized.c_str());
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "newline-only manifest exceeded 64 KiB budget");
        write(project / ".ores.pkg.toml", "schema_version = \"1\"\n");
        const auto expected = fs::canonical(project / "src" / "lib" / "answer.ores");
        check(project_root_source(project, "+/src/lib/answer") == expected,
              "canonical root source unresolved");
        // The manifest's explicit project.root changes the authority anchor.
        write(project / "appdata" / "lib" / "answer.ores",
              "fnc answer():int{return 42;}\n");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[project]\nroot = \"appdata\"\n");
        check(project_root_source(project, "+/lib/answer") ==
                  fs::canonical(project / "appdata/lib/answer.ores"),
              "explicit project.root ignored");
        write(project / "appdata" / ".ores.pkg.toml", "schema_version = \"1\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "nested package reused parent-root authority");
        fs::remove(project / "appdata" / ".ores.pkg.toml");
        write(project / "parent" / "appdata" / "lib" / "answer.ores",
              "fnc answer():int{return 42;}\n");
        write(project / "parent" / ".ores.pkg.toml", "schema_version = \"1\"\n");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[project]\nroot = \"parent/appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "intermediate nested package marker ignored");
        fs::remove(project / "parent" / ".ores.pkg.toml");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[project]\nroot = \"appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
               "rooted import leaked manifest parent after explicit root");
        for (const char* bad_root : {"../other", "/tmp", "appdata/../src", "",
                                     "appdata\\outside", "appdata//lib", "appdata/", "appdata/."}) {
            write(project / ".ores.pkg.toml",
                  ("schema_version = \"1\"\n[project]\nroot = \"" +
                   std::string(bad_root) + "\"\n").c_str());
            reject([&] { (void)project_root_source(project, "+/lib/answer"); },
                   "unsafe project.root admitted");
        }
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[project]\nroot = \"appdata\"\nroot = \"appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "duplicate project.root admitted");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[ project ]\nroot = \"appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "unsupported project table silently ignored");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\n[project]\n[project]\nroot = \"appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "duplicate project table admitted");
        write(project / ".ores.pkg.toml",
              "schema_version = \"1\"\nproject.root = \"appdata\"\n");
        reject([&] { (void)project_root_source(project, "+/lib/answer"); },
               "unsupported dotted project.root accepted");
        write(project / ".ores.pkg.toml", "schema_version = \"1\"\n");

        check(project_root_source(project, "+/src/lib/answer.ores") == expected,
              "exact .ores source unresolved");
        for (const std::string& bad : std::vector<std::string>{
            "", "+", "+src/lib/answer", "@/", "@/src/lib/answer",
            "+/", "+//src/lib/answer", "+/src//answer", "+/src/./lib/answer",
            "+/src/../src/lib/answer", "+/src/lib/answer/",
            "+/src\\lib\\answer", "+/src:lib/answer", "+/unknown",
            "+/src/lib/answer.java", std::string("+/src/") + char(0) + "thing"
        }) {
            reject([&] { (void)project_root_source(project, bad); },
                   "invalid canonical +/ source accepted");
        }
        reject([&] { (void)project_root_source(project, std::string("+/") +
            std::string(4096, 'x')); }, "oversized rooted import accepted");
        std::error_code error;
        fs::create_symlink(tmp / "other" / "answer.ores",
                           project / "src" / "lib" / "foreign.ores", error);
        if (!error)
            reject([&] { (void)project_root_source(project, "+/src/lib/foreign"); },
                   "foreign source symlink escaped package boundary");
        fs::create_symlink(tmp / "other" / "answer.ores",
                           project / ".ores.pkg.toml.injected", error);
        // A manifest symlink to a nested file must not confer project-root
        // authority, even if the target contains a plausible v1 marker.
        write(project / "src" / "foreign-manifest.toml", "schema_version = \"1\"\n");
        fs::remove(project / ".ores.pkg.toml");
        error.clear();
        fs::create_symlink(project / "src" / "foreign-manifest.toml",
                           project / ".ores.pkg.toml", error);
        if (!error) {
            reject([&] { (void)project_root_source(project, "+/src/lib/answer"); },
                   "manifest symlink delegated root authority into child directory");
            fs::remove(project / ".ores.pkg.toml");
        }
        write(project / ".ores.pkg.toml", "schema_version = \"1\"\n");
        // Lexical nested-package crossing cannot be legalized by a symlink
        // pointing back to the parent-owned physical source.
        write(project / "nested" / ".ores.pkg.toml", "schema_version = \"1\"\n");
        error.clear();
        fs::create_symlink(project / "src" / "lib" / "answer.ores",
                           project / "nested" / "backref.ores", error);
        if (!error)
            reject([&] { (void)project_root_source(project, "+/nested/backref"); },
                   "nested-package symlink alias escaped namespace boundary");
        check(project_root_source(project, "+/src/lib/answer") == expected,
              "legal parent-owned source rejected after nested checks");
        fs::create_directories(tmp / "another");
        write(tmp / "another" / ".ores.pkg.toml", "schema_version = \"1\"\n");
        check(project_root_source(project, "+/src/lib/answer") == expected,
              "unrelated sibling manifest changed package identity");
        std::cout << "Forge canonical +/ resolver host cases passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Forge root resolver failed: " << ex.what() << '\n';
        return 1;
    }
}
