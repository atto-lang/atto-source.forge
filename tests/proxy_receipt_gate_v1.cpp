#include "atto/proxy_receipt_gate_v1.hpp"
#include <cassert>

using namespace atto::ipc;
int main() {
    const receipt_identity id{1, 11, 12, 13, 14, 17, 15, 16, 1};
    proxy_receipt_gate gate{id};
    assert(!gate.destination_admitted()); // proxy queue and socket write do not ACK
    assert(!gate.apply_verified(id, receipt_event::started));
    assert(!gate.apply_verified(id, receipt_event::processed));
    assert(!gate.apply_verified(id, receipt_event::durable_inbox));
    assert(!gate.apply_verified(id, receipt_event::durable_result));
    auto wrong = id;
    wrong.tenant++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.caller_incarnation++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.target_incarnation++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.association_generation++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.placement_epoch++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.attempt++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    wrong = id; wrong.protocol_major++;
    assert(!gate.apply_verified(wrong, receipt_event::admitted));
    assert(gate.apply_verified(id, receipt_event::admitted));
    assert(gate.destination_admitted());
    assert(!gate.apply_verified(id, receipt_event::durable_result));
    assert(gate.apply_verified(id, receipt_event::durable_inbox));
    assert(gate.durable_inbox() && gate.stage() == receipt_stage::admitted);
    assert(gate.apply_verified(id, receipt_event::started));
    assert(gate.apply_verified(id, receipt_event::admitted)); // late duplicate
    assert(gate.stage() == receipt_stage::started);
    assert(gate.apply_verified(id, receipt_event::processed));
    assert(gate.apply_verified(id, receipt_event::durable_result));
    assert(gate.durable_result());
    assert(!gate.apply_verified(id, receipt_event::failed));
    assert(!gate.apply_verified(id, receipt_event::indeterminate));
    proxy_receipt_gate dropped{id};
    assert(dropped.apply_verified(id, receipt_event::indeterminate));
    assert(dropped.outcome() == receipt_outcome::indeterminate);
    assert(!dropped.apply_verified(id, receipt_event::admitted)); // late ACK
    proxy_receipt_gate rejected{id};
    assert(rejected.apply_verified(id, receipt_event::rejected));
    assert(!rejected.destination_admitted());
    assert(!rejected.apply_verified(id, receipt_event::admitted));
    proxy_receipt_gate failed{id};
    assert(failed.apply_verified(id, receipt_event::admitted));
    assert(failed.apply_verified(id, receipt_event::failed));
    assert(!failed.apply_verified(id, receipt_event::processed));
    proxy_receipt_gate failclosed{{1, 0, 12, 13, 14, 17, 15, 16, 1}};
    assert(!failclosed.apply_verified({1, 0, 12, 13, 14, 17, 15, 16, 1},
                                     receipt_event::admitted));
    return 0;
}
