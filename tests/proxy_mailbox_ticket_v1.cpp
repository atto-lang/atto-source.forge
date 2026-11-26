#include "atto/proxy_mailbox_ticket_v1.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <cstdio>
using namespace atto::ipc;
static receipt_identity route(std::uint64_t id, std::uint64_t incarnation = 14) {
    return {1, 11, 12, 13, incarnation, 17, 15, id, 1};
}
static host_ticket_ptr admit(host_proxy_mailbox_tickets& m, receipt_identity id,
                             std::int64_t value) {
    auto r = m.admit(id, value);
    assert(r.status == host_ticket_admission::admitted && r.ticket);
    return r.ticket;
}
static void correlation_and_full() {
    host_proxy_mailbox_tickets a(11, 13, 14, 1), b(11, 13, 14, 1);
    auto wrong_tenant = route(101);
    wrong_tenant.tenant = 88;
    assert(a.admit(wrong_tenant, 7).status == host_ticket_admission::stale);
    assert(!a.dispatch());
    auto first = admit(a, route(101), 11);
    auto first_turn = a.dispatch();
    assert(first_turn && first_turn->value() == 11);
    auto queued = admit(a, route(102), 12);
    auto full = a.admit(route(103), 13);
    assert(full.status == host_ticket_admission::full && !full.ticket);
    auto other = admit(b, route(104), 70);
    auto other_turn = b.dispatch();
    assert(other_turn && b.commit(*other_turn, 140));
    assert(b.snapshot(other)->result == 140);
    assert(a.snapshot(first)->state == host_ticket_state::started);
    assert(a.snapshot(queued)->state == host_ticket_state::admitted);
    assert(!a.commit(*other_turn, 999) && !b.commit(*first_turn, 999));
    assert(!a.snapshot(other));
    auto bad = route(105);
    bad.tenant = 0;
    assert(a.admit(bad, 1).status == host_ticket_admission::invalid);
    assert(a.admit(route(105, 99), 1).status == host_ticket_admission::stale);
    assert(a.commit(*first_turn, 111));
    auto second_turn = a.dispatch();
    assert(second_turn && second_turn->value() == 12);
    assert(a.commit(*second_turn, 112));
    assert(a.snapshot(first)->result == 111 && a.snapshot(queued)->result == 112);
    assert(a.conservation() && b.conservation());
}
static void concurrent_results() {
    host_proxy_mailbox_tickets m(11, 13, 14, 256);
    std::mutex sent_lock;
    std::vector<std::pair<std::int64_t, host_ticket_ptr>> sends;
    std::array<std::thread, 4> producers;
    for (int p = 0; p < 4; ++p) {
        producers[p] = std::thread([&m, &sent_lock, &sends, p] {
            for (int i = 0; i < 32; ++i) {
                std::int64_t value = p * 1000LL + i + 1;
                auto ticket = admit(m, route(static_cast<std::uint64_t>(
                    p * 1000 + i + 3000)), value);
                std::lock_guard lock(sent_lock);
                sends.emplace_back(value, ticket);
            }
        });
    }
    for (auto& producer : producers) producer.join();
    assert(sends.size() == 128);
    for (int i = 0; i < 128; ++i) {
        auto delivery = m.dispatch();
        assert(delivery && m.commit(*delivery, delivery->value() * 3));
    }
    for (const auto& sent : sends) {
        auto snap = m.snapshot(sent.second);
        assert(snap && snap->state == host_ticket_state::processed &&
               snap->has_result && snap->result == sent.first * 3);
    }
    assert(m.conservation());
}
static void panic_restart_close() {
    host_proxy_mailbox_tickets m(11, 13, 14, 2);
    auto failed = admit(m, route(201), -1);
    auto turn = m.dispatch();
    auto abandoned = admit(m, route(202), 20);
    assert(turn && m.panic(*turn));
    assert(m.snapshot(failed)->state == host_ticket_state::failed);
    assert(m.snapshot(abandoned)->state == host_ticket_state::abandoned);
    assert(!m.commit(*turn, 99));
    assert(m.admit(route(203), 1).status == host_ticket_admission::closed);
    assert(!m.dispatch() && m.conservation());
    host_proxy_mailbox_tickets next(11, 13, 15, 1);
    assert(next.admit(route(204), 1).status == host_ticket_admission::stale);
    assert(!next.snapshot(failed));
    auto fresh = admit(next, route(205, 15), 8);
    auto new_turn = next.dispatch();
    assert(new_turn && next.commit(*new_turn, 50));
    assert(next.snapshot(fresh)->result == 50 && next.conservation());
    host_proxy_mailbox_tickets draining(11, 13, 14, 1);
    auto old = admit(draining, route(300), 3);
    draining.close();
    assert(draining.admit(route(301), 3).status == host_ticket_admission::closed);
    auto last = draining.dispatch();
    assert(last && draining.commit(*last, 4));
    assert(draining.snapshot(old)->result == 4 && draining.conservation());
}
int main() {
    correlation_and_full();
    concurrent_results();
    panic_restart_close();
    std::puts("ATTO_FORGE_HOST_PER_MAIL_TICKETS_128_CONCURRENT=PASS");
    std::puts("ATTO_FORGE_SCOPE=HOST_ONLY_NO_COMPILED_ATTO_PROXY");
}
