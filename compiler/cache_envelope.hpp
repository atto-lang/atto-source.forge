#pragma once
// Trusted host-only receipt substrate; not authority to execute or load.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace oresforge::cache {
using Bytes=std::vector<std::uint8_t>;
using Secret=std::array<std::uint8_t,32>;
struct Dependency {std::string_view identity,digest;};
struct Input {
 std::string_view stage,owner,unit_kind,unit,compiler,toolchain,target,configuration,sources,bindings;
 std::optional<std::string_view> generated;
 std::span<const Dependency> dependencies;
};
std::string encode(const Input&);
Bytes seal(std::string_view key,std::span<const std::uint8_t> artifact,const Secret&);
Bytes verify(std::string_view key,std::span<const std::uint8_t> envelope,const Secret&);
// Trusted external current pin, not a durable rollback-proof provider.
Bytes ledger_record(std::string_view scope,std::uint64_t epoch,const Secret& master);
Secret ledger_admit(std::string_view scope,std::span<const std::uint8_t> record,const Secret& current_pin,const Secret& master);
}
