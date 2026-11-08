#include "compiler/actor_source.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

#define REQUIRE(x) do { if (!(x)) throw std::runtime_error( \
    "actor source admission regression at line " + std::to_string(__LINE__)); } while (0)

static std::string fixture(std::string_view actor, std::string_view field,
                           std::string_view message, std::string_view initial,
                           std::string_view operator_) {
    return "define actor " + std::string(actor) + " as\n"
           "  let int " + std::string(field) + ": " + std::string(initial) + "\n"
           "  receive(int " + std::string(message) + ") -> int ::\n"
           "    " + std::string(field) + " = " + std::string(field) +
                     " " + std::string(operator_) + " " + std::string(message) + "\n"
           "    return " + std::string(field) + "\n"
           "  end\nend\n";
}
static void deny(std::string_view text) {
    bool rejected = false;
    try { (void)oresforge::actor_source::emit_cxx(text); }
    catch (const std::runtime_error& e) {
        rejected = std::string_view(e.what()).find("actor source:") != std::string_view::npos;
    }
    REQUIRE(rejected);
}
static std::string replace_once(std::string s, std::string_view a,
                                std::string_view b) {
    const auto pos = s.find(a);
    REQUIRE(pos != std::string::npos);
    s.replace(pos, a.size(), b);
    return s;
}

int main() {
    std::size_t positive = 0, negative = 0;
    const std::array<std::string_view, 7> initializers{
        "0", "1", "-1", "9223372036854775807", "-9223372036854775808",
        "17", "-239"
    };
    const std::array<std::string_view, 2> operations{"+", "-"};
    for (const auto init : initializers) {
        for (const auto op : operations) {
            for (int i = 0; i < 48; ++i) {
                const auto source = fixture("Actor" + std::to_string(i),
                                            "field" + std::to_string(i),
                                            "arg" + std::to_string(i), init, op);
                const auto generated = oresforge::actor_source::emit_cxx(source);
                REQUIRE(!generated.empty());
                REQUIRE(generated == oresforge::actor_source::emit_cxx(source));
                REQUIRE(generated.find("frames.push(token, 16)") != std::string::npos);
                REQUIRE(generated.find("frames.pop(token, frame)") != std::string::npos);
                REQUIRE(generated.find("ores_actor_i64_heap_field_write") != std::string::npos);
                REQUIRE(generated.find("throw std::overflow_error") != std::string::npos);
                REQUIRE(generated.find(op == "+" ? "old + argument" : "old - argument")
                        != std::string::npos);
                REQUIRE(generated.find("Actor" + std::to_string(i)) == std::string::npos);
                REQUIRE(generated.find("field" + std::to_string(i)) == std::string::npos);
                REQUIRE(generated.find("arg" + std::to_string(i)) == std::string::npos);
                ++positive;
            }
        }
    }
    // Source identifiers never become interpolated native C++ tokens.
    REQUIRE(oresforge::actor_source::emit_cxx(fixture("Foo","value","delta","7","+")) ==
            oresforge::actor_source::emit_cxx(fixture("Bar","state","change","7","+")));
    ++positive;
    // CRLF and horizontal tabs are ordinary comment formatting, not execution.
    const auto crlf = "//\t Header\r\n" + fixture("Counter","total","amount","2","+");
    REQUIRE(!oresforge::actor_source::emit_cxx(crlf).empty());
    ++positive;

    const auto valid = fixture("Counter","total","amount","0","+");
    const std::array<std::pair<std::string_view, std::string_view>, 21> rewrites{{
        {"define actor", "define class"},
        {"define actor", "pub define actor"},
        {"let int", "const int"},
        {"let int", "int"},
        {"receive(int", "suspends receive(int"},
        {"receive(int", "receive(unknown"},
        {"-> int", "-> void"},
        {"-> int ::", "-> int {}"},
        {"total = total + amount", "total = amount"},
        {"total = total + amount", "total = total * amount"},
        {"total = total + amount", "total = total % amount"},
        {"total = total + amount", "total = total ^ amount"},
        {"total = total + amount", "total = total xor amount"},
        {"total = total + amount", "total = total + await amount"},
        {"return total", "return amount"},
        {"return total", "return void"},
        {"return total", "return total + 1"},
        {"return total", "select total"},
        {"  end\nend\n", "  end\nend\nimport evil\n"},
        {"  end\nend\n", "  end\nend\nfnc malicious() -> int { return 1; }\n"},
        {"  end\nend\n", "  end\nend\n" }
    }};
    for (const auto& [old_text, new_text] : rewrites) {
        auto bad = replace_once(valid, old_text, new_text);
        if (old_text == "  end\nend\n" && new_text == "  end\nend\n")
            bad += valid; // deliberate duplicate actor
        deny(bad);
        ++negative;
    }
    for (const auto name : {
        "class", "enum", "fnc", "critical", "suspends", "await",
        "pub", "const", "mut", "select", "match", "true", "false",
        "rt", "borrow", "self", "null", "while", "xor",
        "routine", "defer", "import", "return", "state"
    }) {
        deny(fixture("Counter", name, "amount", "0", "+"));
        deny(fixture("Counter", "total", name, "0", "+"));
        deny(fixture(name, "total", "amount", "0", "+"));
        negative += 3;
    }
    for (const auto initial : {
        "9223372036854775808", "-9223372036854775809", "18446744073709551616",
        "-18446744073709551616", "--1", "+1", "1.5", "0x42", "1e8"
    }) {
        deny(fixture("Counter", "total", "amount", initial, "+"));
        ++negative;
    }
    deny(fixture("Counter", "total", "total", "0", "+")); ++negative;
    deny(std::string{}); ++negative;
    deny(valid + '\0'); ++negative;
    deny(valid + "\xff"); ++negative;
    deny(valid + "\x7f"); ++negative;
    deny(valid + "\r\n#host-source\n"); ++negative;
    deny("//\x01\n" + valid); ++negative;
    deny(valid + std::string(8192, ' ')); ++negative;

    // Each unknown grammar token must fail before emit_cxx returns a result.
    for (int i = 0; i < 256; ++i) {
        const auto index = valid.find(" = ");
        REQUIRE(index != std::string::npos);
        auto bad = valid;
        bad.insert(index + 1, std::string((i % 5) + 1, '@'));
        deny(bad);
        ++negative;
    }
    std::printf("Forge actor source: %zu deterministic positives, "
                "%zu fail-closed negatives PASS\n", positive, negative);
}
