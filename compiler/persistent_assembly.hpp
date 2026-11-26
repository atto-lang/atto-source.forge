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
// Closed explicit provider inventory, not a general package manifest.
// Host independently approves both current pins and serializes epoch rotation.
struct ProviderInput {
 std::string_view path,name_space,symbol,source;
 std::size_t arity;
};
AssemblyBuild compile_cached_namespace_provider(const std::filesystem::path& root,
 std::span<const ProviderInput> inventory,std::string_view expected_manifest,std::string_view expected_sources,
 std::string_view name_space,std::string_view symbol,std::size_t arity,
 std::string_view scope,std::string_view compiler,std::string_view toolchain,
 std::span<const std::uint8_t> record,const Secret& pin,const Secret& master,bool cold=false);
}
