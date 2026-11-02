#include "compiler/frontend.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

static void negative(std::string_view code, std::string_view expected) {
    try {
        (void)oresforge::parse_source(code);
    } catch (const std::runtime_error& error) {
        if (std::string_view(error.what()).find(expected) != std::string_view::npos) return;
        throw std::runtime_error(std::string("wrong rejection: ") + error.what());
    }
    throw std::runtime_error("unexpectedly accepted unsupported critical/suspends source");
}

int main() {
    negative("critical fnc main(): int { return 1; }", "E_CRITICAL_UNLOWERED");
    negative("fnc main(): int { critical { return 1; } }", "E_CRITICAL_UNLOWERED");
    negative("fnc main(): int { critical gate { return 1; } }", "E_CRITICAL_UNLOWERED");
    negative("fnc main(): int { suspends critical { return 1; } }", "E_SUSPENDS_UNLOWERED");
    negative("suspends fnc main(): int { return 1; }", "E_SUSPENDS_UNLOWERED");
    negative("critical suspends fnc main(): int { return 1; }", "E_CRITICAL_SUSPENDS_DECL");
    negative("suspends critical fnc main(): int { return 1; }", "E_CRITICAL_SUSPENDS_DECL");
    negative("pub critical /* nested /* level */ end */ suspends fnc main(): int { return 1; }", "E_CRITICAL_SUSPENDS_DECL");
    negative("pub suspends // comment\\n critical fnc main(): int { return 1; }", "E_CRITICAL_SUSPENDS_DECL");
    negative("async fnc main(): int { return 1; }", "E_ASYNC_FUNCTION_REMOVED");
    negative("async routine main(): int { return 1; }", "E_ASYNC_FUNCTION_REMOVED");
    negative("async generator fnc main(): int { yield 1; }", "E_ASYNC_UNLOWERED");
    // Plain scalar compilation continues to work: no global word substitution.
    (void)oresforge::parse_source("fnc main(): int { return 7; }");
    // Critical-looking text inside a comment cannot grant access or trip admission.
    (void)oresforge::parse_source("// critical fnc is a comment\nfnc main(): int { return 7; }");
    return 0;
}
