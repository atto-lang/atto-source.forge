#pragma once
// Fail-closed allocation/JIT policy admission. This backend has no executable
// JIT or heap object lowering: parsing a flag never implies optimization.
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::cli {
struct OptimizationOptions {
  std::string escape = "auto";
  std::string jit = "off";
  bool report = false;
  std::vector<char*> positional;
};

inline bool has_prefix(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

inline bool parse_bool(std::string_view value) {
  if (value == "true" || value == "1") return true;
  if (value == "false" || value == "0") return false;
  throw std::runtime_error("invalid --opt-escape-report boolean; expected true or false");
}

inline OptimizationOptions parse_optimization_options(int argc, char** argv) {
  OptimizationOptions options;
  if (const char* value = std::getenv("ORES_OPT_ESCAPE")) options.escape = value;
  if (const char* value = std::getenv("ORES_JIT")) options.jit = value;
  // Do not validate mapped env values until CLI overrides have been parsed.
  // In particular, an invalid mapped bool must not defeat an explicit
  // --opt-escape-report=false (direct flags have higher precedence).
  const char* mapped_report = std::getenv("ORES_OPT_ESCAPE_REPORT");

  // Support direct flags as well as flags-2-env. Explicit CLI overrides env;
  // duplicate CLI spellings are an error rather than a last-writer-wins rule.
  options.positional.push_back(argv[0]);
  bool seen_escape = false;
  bool seen_jit = false;
  bool seen_report = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (has_prefix(arg, "--opt-escape=")) {
      if (seen_escape) throw std::runtime_error("duplicate --opt-escape flag");
      seen_escape = true;
      options.escape = std::string(arg.substr(sizeof("--opt-escape=") - 1));
    } else if (has_prefix(arg, "--jit=")) {
      if (seen_jit) throw std::runtime_error("duplicate --jit flag");
      seen_jit = true;
      options.jit = std::string(arg.substr(sizeof("--jit=") - 1));
    } else if (arg == "--opt-escape-report" ||
               has_prefix(arg, "--opt-escape-report=")) {
      if (seen_report) throw std::runtime_error("duplicate --opt-escape-report flag");
      seen_report = true;
      options.report = arg == "--opt-escape-report"
        ? true : parse_bool(arg.substr(sizeof("--opt-escape-report=") - 1));
    } else if (arg == "--opt-escape" || arg == "--jit" ||
               has_prefix(arg, "--opt-escape-") || has_prefix(arg, "--jit-")) {
      throw std::runtime_error("invalid optimization flag syntax");
    } else {
      options.positional.push_back(argv[i]);
    }
  }

  if (!seen_report && mapped_report)
    options.report = parse_bool(mapped_report);

  if (options.escape != "auto" && options.escape != "off" && options.escape != "safe" &&
      options.escape != "verify" && options.escape != "speculative")
    throw std::runtime_error("invalid --opt-escape mode; expected auto|off|safe|verify|speculative");
  if (options.jit != "on" && options.jit != "off")
    throw std::runtime_error("invalid --jit mode; expected on|off");
  if (options.escape == "speculative" && options.jit != "on")
    throw std::runtime_error("--opt-escape=speculative requires --jit=on");

  // Automatic optimization is admitted only for the closed allocation emitter.
  // Other commands retain their previous behavior; explicit safe still rejects
  // unsupported profiles. Architecture and source proof remain checked later.
  if (options.escape == "auto") {
    const bool closed = options.positional.size() == 4 &&
        std::string_view(options.positional[1]) == "emit-alloc-asm";
    options.escape = closed ? "safe" : "off";
  }
  return options;
}

// Enforce once the subcommand is known. Only a strictly parsed closed-world
// allocation fixture may request safe scalarization. All general commands
// remain fail-closed, and the tiered interpreter is never advertised as JIT.
inline void enforce_optimization_support(const OptimizationOptions& options,
                                         bool closed_alloc_asm = false) {
  if (options.jit == "on")
    throw std::runtime_error("--jit=on unsupported: Forge has no executable JIT");
  if (options.escape != "off" &&
      !(closed_alloc_asm && options.escape == "safe"))
    throw std::runtime_error("--opt-escape=" + options.escape +
                             " unsupported: verified escape optimization not implemented");
  if (options.report)
    throw std::runtime_error("--opt-escape-report unsupported: no allocation evidence to report");
}
} // namespace oresforge::cli
