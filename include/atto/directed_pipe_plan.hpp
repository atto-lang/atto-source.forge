#pragma once
// Compiler-only typed DATA graph planning. No executable guest transport,
// PipeHandle, ActorRef issuance, TenantVM capability minting or CONTROL route.
// Input stages MUST come from the nominal compiler/typechecker and an
// independently validated capability registry; caller strings prove nothing.
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace atto::forge::pipe {
enum class Direction { pipe, rpipe };
enum class Endpoint { actor, channel, stream_readable, stream_sink, stream_duplex };
struct Stage {
  std::string id;
  std::string tenant;
  Endpoint endpoint;
  std::optional<std::string> receives;
  std::optional<std::string> emits;
};
struct Edge {
  std::size_t source_written_index;
  std::size_t sink_written_index;
  std::string source;
  std::string sink;
  std::string payload;
};
struct Plan {
  Direction direction;
  std::string tenant;
  std::vector<Stage> written_stages;
  std::vector<std::size_t> flow_indices;
  std::vector<Edge> data_edges;
};
class Error final : public std::runtime_error {
 public:
  explicit Error(const char* code) : std::runtime_error(code) {}
};
namespace detail {
inline bool valid_label(const std::string& value, bool type) noexcept {
  if (value.empty() || value.size() > 256) return false;
  const auto first = value.front();
  if (!((first >= 'A' && first <= 'Z') ||
        (first >= 'a' && first <= 'z') || first == '_')) return false;
  for (const char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' ||
        (type ? (c == '.' || c == '<' || c == '>') : c == '-'))
      continue;
    return false;
  }
  return true;
}
inline std::string scoped(const std::string& tenant, const std::string& id) {
  // Neither component can contain NUL after validation.
  std::string key = tenant;
  key.push_back('\0');
  key += id;
  return key;
}
}  // namespace detail

[[nodiscard]] inline Plan plan(const std::vector<Stage>& written,
                                Direction direction) {
  if (written.size() < 2 || written.size() > 16)
    throw Error("E_PIPE_STAGE_LIMIT");
  const auto tenant = written.front().tenant;
  std::unordered_set<std::string> ids;
  for (const auto& stage : written) {
    if (!detail::valid_label(stage.id, false)) throw Error("E_PIPE_ID");
    if (!detail::valid_label(stage.tenant, false))
      throw Error("E_PIPE_TENANT_ID");
    if (stage.tenant != tenant) throw Error("E_PIPE_TENANT_BOUNDARY");
    if (!ids.insert(stage.id).second) throw Error("E_PIPE_DUPLICATE_STAGE");
    if ((stage.receives && !detail::valid_label(*stage.receives, true)) ||
        (stage.emits && !detail::valid_label(*stage.emits, true)))
      throw Error("E_PIPE_TYPE_LABEL");
    switch (stage.endpoint) {
      case Endpoint::actor:
        if (!stage.receives && !stage.emits) throw Error("E_PIPE_PORTS");
        break;
      case Endpoint::channel:
      case Endpoint::stream_duplex:
        if (!stage.receives || !stage.emits || stage.receives != stage.emits)
          throw Error("E_PIPE_CHANNEL_PORTS");
        break;
      case Endpoint::stream_readable:
        if (stage.receives || !stage.emits)
          throw Error("E_PIPE_READONLY_PORT");
        break;
      case Endpoint::stream_sink:
        if (!stage.receives || stage.emits)
          throw Error("E_PIPE_WRITEONLY_PORT");
        break;
      default:
        throw Error("E_PIPE_ENDPOINT");
    }
  }
  Plan result{direction, tenant, written, {}, {}};
  result.flow_indices.reserve(written.size());
  result.data_edges.reserve(written.size() - 1);
  for (std::size_t i = 0; i < written.size(); ++i)
    result.flow_indices.push_back(direction == Direction::pipe
                                      ? i : written.size() - i - 1);
  for (std::size_t i = 0; i + 1 < written.size(); ++i) {
    const auto from = result.flow_indices[i];
    const auto to = result.flow_indices[i + 1];
    const auto& source = written[from];
    const auto& sink = written[to];
    if (!source.emits) throw Error("E_PIPE_MISSING_EMITS");
    if (!sink.receives) throw Error("E_PIPE_MISSING_RECEIVES");
    if (*source.emits != *sink.receives) throw Error("E_PIPE_TYPE_MISMATCH");
    result.data_edges.push_back(Edge{from, to, source.id, sink.id,
                                     *source.emits});
  }
  return result;
}

// Cross-expression *compiler* preflight. A real runtime MUST atomically
// reserve BOTH endpoints with authenticated tenant/generation leases and
// roll back on error. Explicit tee/merge adapters own their separate ports.
inline void check_unicast(const std::vector<Plan>& plans) {
  if (plans.size() > 64) throw Error("E_PIPE_PLAN_LIMIT");
  std::unordered_set<std::string> pairs;
  std::unordered_set<std::string> outputs;
  std::unordered_set<std::string> inputs;
  for (const auto& item : plans) {
    for (const auto& edge : item.data_edges) {
      const auto source = detail::scoped(item.tenant, edge.source);
      const auto sink = detail::scoped(item.tenant, edge.sink);
      const auto pair = detail::scoped(source, sink);
      if (pairs.contains(pair)) throw Error("E_PIPE_DUPLICATE_EDGE");
      if (outputs.contains(source)) throw Error("E_PIPE_OUTPUT_OCCUPIED");
      if (inputs.contains(sink)) throw Error("E_PIPE_INPUT_OCCUPIED");
      pairs.insert(pair);
      outputs.insert(source);
      inputs.insert(sink);
    }
  }
}
}  // namespace atto::forge::pipe
