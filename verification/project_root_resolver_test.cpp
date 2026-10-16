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
        write(project / ".ores.pkg.toml", "schema_version = \"1\"\n");
        const auto expected = fs::canonical(project / "src" / "lib" / "answer.ores");
        check(project_root_source(project, "+/src/lib/answer") == expected,
              "canonical root source unresolved");
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
