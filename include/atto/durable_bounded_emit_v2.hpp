#pragma once
#include "ores/durable/bounded_checkpoint_v2.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace atto::forge {
// Trusted compiler-owned manifest only. No source admission or disk authority.
// Share the exact reference codec implementation; only the schema is emitted.
inline std::string emit_durable_bounded_v2(std::string_view actor,
        const std::vector<ores::durable::BoundedCheckpointV2::Field>& fields) {
    using C=ores::durable::BoundedCheckpointV2;
    const C validated(actor,fields); // checks names, kinds, count and duplicates
    std::string out="#pragma once\n#include \"ores/durable/bounded_checkpoint_v2.hpp\"\n"
        "namespace atto_generated {\nusing Codec=ores::durable::BoundedCheckpointV2;\n"
        "inline const Codec& schema(){static const Codec c(\"";
    out+=actor;out+="\", {";
    for(const auto& field:fields){
        out+="{\""+field.name+"\",static_cast<Codec::Kind>("+std::to_string(static_cast<unsigned>(field.kind))+")},";
    }
    out+="});return c;}\n";
    out+="inline constexpr std::uint64_t schema_id="+std::to_string(validated.fingerprint())+"ULL;\n";
    out+="inline std::string encode(const std::vector<Codec::Value>& values){return schema().encode(values);}\n"
         "inline std::vector<Codec::Value> decode(std::string_view bytes){return schema().decode(bytes);}\n}\n";
    return out;
}
} // namespace atto::forge
