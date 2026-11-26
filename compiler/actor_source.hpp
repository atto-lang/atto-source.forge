#pragma once
#include <string>
#include <string_view>
namespace oresforge::actor_source {
// Opt-in closed-world stage-0 AttoLang actor subset. Emitted C++ is trusted
// native code, compiled and linked against Forge's actual ScalarActor.
// Not general actor lowering and not a self-hosted compiler.
std::string emit_cxx(std::string_view source);
}
