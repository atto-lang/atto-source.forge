#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>

// Actual production Forge C++ frontend, not a regex stub or host reimplementation.
// This is an admission test only; it does NOT claim executable std/time guest support.
static void must_reject(std::string_view source) {
    bool rejected=false;
    try {
        (void)oresforge::parse_source(source);
    } catch (const std::runtime_error&) {
        rejected=true;
    }
    if (!rejected) throw std::runtime_error("Forge silently admitted unsupported time source");
}

int main() {
    std::ifstream file("stdlib/std/time.atto",std::ios::binary);
    if(!file)throw std::runtime_error("missing backend-owned std/time.atto");
    const std::string time((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    assert(time.find("define module time as")!=std::string::npos);
    assert(time.find("define pub class Duration as")!=std::string::npos);
    assert(time.find("pub fnc manual_clock(")!=std::string::npos);
    must_reject(time);
    must_reject("import module time from \"std/time\"; fnc main():int{return 42;}");
    must_reject("fnc main():int{await time.sleep(100);return 42;}");
    must_reject("fnc main():int{return time.seconds(1);}");

    const auto program=oresforge::parse_source("fnc main():int{return 42;}");
    assert(oresforge::interpret(program)==42);
    std::cout<<"FORGE_STD_TIME_ADMISSION_PASS: 4 negative cases and scalar guest 42\n";
}
