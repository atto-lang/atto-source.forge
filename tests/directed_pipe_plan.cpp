// Real native C++ compiler-planner assertions; NOT a compiled Atto guest test.
#include "atto/directed_pipe_plan.hpp"
#include <cassert>
#include <functional>
#include <string>
#include <vector>
using namespace atto::forge::pipe;
namespace {
constexpr auto tenant = "tenant_a";
Stage actor(std::string id, std::optional<std::string> in,
            std::optional<std::string> out, std::string t = tenant) {
  return {std::move(id), std::move(t), Endpoint::actor,
          std::move(in), std::move(out)};
}
Stage producer(std::string id = "producer") {
  return actor(std::move(id), "int", "int");
}
Stage consumer(std::string id = "consumer") {
  return actor(std::move(id), "int", std::nullopt);
}
Stage channel(std::string id = "buffer") {
  return {std::move(id), tenant, Endpoint::channel, "int", "int"};
}
void rejects(const char* code, const std::function<void()>& action) {
  bool caught = false;
  try { action(); }
  catch (const Error& e) { caught = e.what() == std::string(code); }
  assert(caught && "wrong or missing compiler diagnostic");
}
}  // namespace
int main() {
  const auto forward = plan({producer(), channel(), consumer()}, Direction::pipe);
  const auto reverse = plan({consumer(), channel(), producer()}, Direction::rpipe);
  assert(forward.flow_indices == std::vector<std::size_t>({0,1,2}));
  assert(reverse.flow_indices == std::vector<std::size_t>({2,1,0}));
  assert(reverse.written_stages.front().id == "consumer");
  assert(forward.data_edges.size() == 2 && reverse.data_edges.size() == 2);
  for (std::size_t i = 0; i < 2; ++i) {
    assert(forward.data_edges[i].source == reverse.data_edges[i].source);
    assert(forward.data_edges[i].sink == reverse.data_edges[i].sink);
    assert(forward.data_edges[i].payload == "int");
  }
  const auto first = plan({producer(), consumer()}, Direction::pipe);
  const auto equivalent = plan({consumer(), producer()}, Direction::rpipe);
  rejects("E_PIPE_DUPLICATE_EDGE", [&] { check_unicast({first, equivalent}); });
  const auto fanout = plan({producer(), consumer("sink2")}, Direction::pipe);
  rejects("E_PIPE_OUTPUT_OCCUPIED", [&] { check_unicast({first, fanout}); });
  const auto fanin = plan({producer("source2"), consumer()}, Direction::pipe);
  rejects("E_PIPE_INPUT_OCCUPIED", [&] { check_unicast({first, fanin}); });
  check_unicast({first, plan({producer("source2"), consumer("sink2")}, Direction::pipe)});
  rejects("E_PIPE_MISSING_EMITS", [] { (void)plan({consumer(), producer()}, Direction::pipe); });
  rejects("E_PIPE_MISSING_RECEIVES", [] {
    (void)plan({producer(), {"reader", tenant, Endpoint::stream_readable,
                             std::nullopt, "int"}}, Direction::pipe);
  });
  rejects("E_PIPE_TYPE_MISMATCH", [] {
    (void)plan({producer(), actor("sink", "bool", std::nullopt)}, Direction::pipe);
  });
  rejects("E_PIPE_TENANT_BOUNDARY", [] {
    (void)plan({producer(), actor("foreign", "int", std::nullopt, "tenant_b")}, Direction::pipe);
  });
  rejects("E_PIPE_DUPLICATE_STAGE", [] {
    (void)plan({producer(), producer()}, Direction::pipe);
  });
  rejects("E_PIPE_ID", [] {
    (void)plan({producer(), consumer("bad\xe2\x80\xae")}, Direction::pipe);
  });
  rejects("E_PIPE_TYPE_LABEL", [] {
    (void)plan({producer(), actor("sink", "int\n", std::nullopt)}, Direction::pipe);
  });
  rejects("E_PIPE_CHANNEL_PORTS", [] {
    (void)plan({producer(), {"bad", tenant, Endpoint::channel,
                             "int", "bool"}}, Direction::pipe);
  });
  rejects("E_PIPE_READONLY_PORT", [] {
    (void)plan({producer(), {"bad", tenant, Endpoint::stream_readable,
                             "int", "int"}}, Direction::pipe);
  });
  rejects("E_PIPE_WRITEONLY_PORT", [] {
    (void)plan({producer(), {"bad", tenant, Endpoint::stream_sink,
                             std::nullopt, "int"}}, Direction::pipe);
  });
  rejects("E_PIPE_PORTS", [] {
    (void)plan({producer(), actor("bad", std::nullopt, std::nullopt)}, Direction::pipe);
  });
  rejects("E_PIPE_ENDPOINT", [] {
    (void)plan({producer(), {"bad", tenant, static_cast<Endpoint>(222), "int", "int"}}, Direction::pipe);
  });
  rejects("E_PIPE_STAGE_LIMIT", [] { (void)plan({producer()}, Direction::pipe); });
  rejects("E_PIPE_STAGE_LIMIT", [] { (void)plan(std::vector<Stage>(17, producer()), Direction::pipe); });
  rejects("E_PIPE_PLAN_LIMIT", [&] { check_unicast(std::vector<Plan>(65, first)); });
  const auto source_only = plan({{"reader", tenant, Endpoint::stream_readable,
                                  std::nullopt, "int"}, consumer()}, Direction::pipe);
  const auto sink_only = plan({producer(), {"writer", tenant, Endpoint::stream_sink,
                                           "int", std::nullopt}}, Direction::pipe);
  assert(source_only.data_edges.size() == 1 && sink_only.data_edges.size() == 1);
}
