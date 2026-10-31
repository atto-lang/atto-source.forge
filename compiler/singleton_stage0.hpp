#pragma once
#include "compiler/ir.hpp"
#include "runtime/singleton_identity.hpp"
#include <string>
#include <string_view>

namespace oresforge {

// Deliberately narrow stage-0 bridge for the already-established declaration
// shell: "define [pub] singleton Name as end". This is not the canonical final
// singleton body/API. The normal compiler profiles continue to reject singleton
// source until the full language surface, effects and guest access lowering are
// implemented.
struct SingletonStage0Plan final {
  std::string declaration_name;
  shared::SingletonDeclarationId declaration_id;
  Program initializer_program;
};

// module_identity is trusted compiler input (for example the resolved package
// module path), never guest source. The scalar remainder's main() is used only
// as the stage-0 initializer value so that source checking and execution cross
// the real Forge scalar compiler/interpreter boundary without inventing final
// singleton member syntax.
SingletonStage0Plan compile_singleton_stage0(
    std::string_view source,
    std::string_view module_identity);

} // namespace oresforge
