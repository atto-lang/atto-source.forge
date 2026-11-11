#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>

int main() {
    struct Case { std::string source; std::string diagnostic; };
    const std::vector<Case> rejects {
        {"pub fnc main(): int { return match<int> 99 over on 1 -> {11;} default -> {42;} end; }",
         "MATCH_DEFAULT_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return match 99 over default -> {42;} end; }",
         "MATCH_DEFAULT_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return match 9 over on default -> {42;} end; }",
         "MATCH_DEFAULT_BACKEND_UNSUPPORTED"},
        {"fnc ignored(): int { return match 1 over default -> {42;} end; } pub fnc main(): int { return 42; }",
         "MATCH_DEFAULT_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return ~map<string,int>(\"a\" => 42); }",
         "TILDE_AGGREGATE_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return ~record(foo: 42); }",
         "TILDE_AGGREGATE_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return ~(foo: 42); }",
         "TILDE_AGGREGATE_BACKEND_UNSUPPORTED"},
        {"pub fnc main(): int { return ~(); }",
         "TILDE_AGGREGATE_BACKEND_UNSUPPORTED"}
    };
    std::size_t checked=0;
    for(const auto& item : rejects){
        bool rejected=false;
        try { (void)oresforge::parse_source(item.source); }
        catch(const std::exception& ex){
            if(std::string_view(ex.what()).find(item.diagnostic)==std::string_view::npos ||
               std::string_view(ex.what()).find("byte")==std::string_view::npos)
                throw std::runtime_error(std::string("wrong diagnostic: ")+ex.what());
            rejected=true;
        }
        if(!rejected)throw std::runtime_error("unsupported source generated scalar IR");
        ++checked;
    }
    auto safe = oresforge::parse_source(
        "pub fnc main(): int { /* match<int> x over */ // ~map()\\n return 40 + 2; }");
    if(oresforge::interpret(safe)!=42)
        throw std::runtime_error("baseline scalar execution was changed");
    ++checked;
    std::cout<<"FORGE_MATCH_TILDE_FAILCLOSED_PASS checks="<<checked
             <<" NATIVE_TYPED_AGGREGATES_NOT_IMPLEMENTED\n";
    return 0;
}
