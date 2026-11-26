#include "compiler/actor_source.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) return 2;
        std::string source(8193, '\0');
        in.read(source.data(), static_cast<std::streamsize>(source.size()));
        source.resize(static_cast<std::size_t>(in.gcount()));
        if (in.bad()) return 2;
        std::cout << oresforge::actor_source::emit_cxx(source);
        return std::cout ? 0 : 2;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
