// Actual Forge frontend admission only; no typed codec ABI is claimed.
#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

int main() {
    const std::vector<std::string> rejected{
        "define class User as end pub fnc main(): int { return new User().to_json(); }",
        "define struct User as value: int; end with Serializable; pub fnc main(): int { return 0; }",
        "define interface JsonSerializable as to_json(): string; end pub fnc main(): int { return 0; }",
        "define trait JsonDeserializable as from_json(string input): string; end pub fnc main(): int { return 0; }",
        "pub fnc main(): int { return deserialize(42); }"
    };
    for (const auto& source : rejected) {
        bool denied = false;
        try { (void)oresforge::parse_source(source); }
        catch (const std::exception& error) {
            const std::string_view diagnostic(error.what());
            if (diagnostic.find("byte") == std::string_view::npos)
                throw std::runtime_error("missing source-location diagnostic: " + std::string(diagnostic));
            denied = true;
        }
        if (!denied) throw std::runtime_error("unlowered typed serialization admitted: " + source);
    }
    const auto good = oresforge::parse_source(
        "pub fnc main(): int { /* Serializable to_json() */ return 40 + 2; }");
    if (oresforge::interpret(good) != 42) throw std::runtime_error("scalar control failed");
    std::cout << "FORGE_TYPED_SERDE_ADMISSION_PASS checks=6 NOT_CODEC_EXECUTION\n";
}
