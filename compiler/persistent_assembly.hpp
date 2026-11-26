#pragma once
#include "compiler/cache_envelope.hpp"
#include <filesystem>
namespace oresforge::cache {
// Restricted host-only standalone runtime assembly cache. The installation
// and input identities must be independently attested. Host supplies current
// external ledger pin and excludes noncooperating filesystem writers.
struct AssemblyBuild {Bytes bytes;bool reused;};
AssemblyBuild compile_cached_assembly(const std::filesystem::path& root,
 std::string_view source,std::string_view scope,std::string_view compiler,
 std::string_view toolchain,std::span<const std::uint8_t> record,
 const Secret& pin,const Secret& master,bool cold=false);
}
