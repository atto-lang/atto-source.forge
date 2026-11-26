#include "atto/proxy_mailbox_ticket_v1.hpp"
#include <array>
#include <barrier>
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
static void cross_tenant_same_actor() {
    bool rejected = false;
    try { host_proxy_mailbox_tickets invalid(0, 13, 14, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    host_proxy_mailbox_tickets a(11, 13, 14, 1), b(88, 13, 14, 1);
    auto ra = route(400), rb = ra;
    rb.tenant = 88;
    auto ta = admit(a, ra, 41);
    auto tb = admit(b, rb, 42);
    auto foreign_a = a.admit(rb, 99), foreign_b = b.admit(ra, 99);
    assert(foreign_a.status == host_ticket_admission::stale && !foreign_a.ticket);
    assert(foreign_b.status == host_ticket_admission::stale && !foreign_b.ticket);
    auto va = a.dispatch(), vb = b.dispatch();
    assert(va && vb && va->value() == 41 && vb->value() == 42);
    assert(!a.commit(*vb, 99) && !b.commit(*va, 99));
    assert(!a.snapshot(tb) && !b.snapshot(ta));
    assert(a.commit(*va, 41) && b.commit(*vb, 42));
    assert(a.snapshot(ta)->result == 41 && b.snapshot(tb)->result == 42);
    a.close();
    assert(a.admit(rb, 99).status == host_ticket_admission::stale);
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
static void concurrent_terminal_races() {
    for (std::uint64_t i = 0; i < 64; ++i) {
        host_proxy_mailbox_tickets m(11, 13, 14, 2);
        auto first = admit(m, route(5000 + i), 42);
        auto turn = m.dispatch();
        assert(turn);
        std::barrier start(4);
        bool committed = false, panicked = false;
        host_ticket_admission_result foreign{};
        std::thread commit([&] { start.arrive_and_wait(); committed = m.commit(*turn, 42); });
        std::thread panic([&] { start.arrive_and_wait(); panicked = m.panic(*turn); });
        std::thread attack([&] {
            auto r = route(6000 + i); r.tenant = 88;
            start.arrive_and_wait(); foreign = m.admit(r, 99);
        });
        start.arrive_and_wait();
        commit.join(); panic.join(); attack.join();
        assert(committed != panicked);
        assert(foreign.status == host_ticket_admission::stale && !foreign.ticket);
        auto snap = m.snapshot(first);
        assert(snap && snap->state == (committed ? host_ticket_state::processed : host_ticket_state::failed));
        assert(snap->has_result == committed);
        if (committed) assert(snap->result == 42);
        assert(!m.commit(*turn, 99) && !m.panic(*turn));
        assert(!m.dispatch() && m.conservation());
    }
}
static void concurrent_close_with_queued_mail() {
    for (std::uint64_t i = 0; i < 64; ++i) {
        host_proxy_mailbox_tickets m(11, 13, 14, 1);
        auto first = admit(m, route(7000 + i), 42);
        auto turn = m.dispatch();
        assert(turn);
        auto queued = admit(m, route(8000 + i), 43);
        std::barrier start(5);
        bool committed = false, panicked = false;
        host_ticket_admission_result foreign{};
        std::thread commit([&] { start.arrive_and_wait(); committed = m.commit(*turn, 42); });
        std::thread panic([&] { start.arrive_and_wait(); panicked = m.panic(*turn); });
        std::thread close([&] { start.arrive_and_wait(); m.close(); });
        std::thread attack([&] {
            auto r = route(9000 + i); r.tenant = 88;
            start.arrive_and_wait(); foreign = m.admit(r, 99);
        });
        start.arrive_and_wait();
        commit.join(); panic.join(); close.join(); attack.join();
        assert(committed != panicked);
        assert(foreign.status == host_ticket_admission::stale && !foreign.ticket);
        assert(m.admit(route(10000 + i), 99).status == host_ticket_admission::closed);
        auto a = m.snapshot(first), b = m.snapshot(queued);
        assert(a && b && a->has_result == committed && !b->has_result);
        assert(a->state == (committed ? host_ticket_state::processed : host_ticket_state::failed));
        if (committed) {
            assert(a->result == 42 && b->state == host_ticket_state::admitted);
            auto drain = m.dispatch();
            assert(drain && drain->value() == 43 && m.commit(*drain, 43));
            auto settled = m.snapshot(queued);
            assert(settled && settled->state == host_ticket_state::processed && settled->has_result && settled->result == 43);
        } else {
            assert(b->state == host_ticket_state::abandoned);
        }
        assert(!m.dispatch() && m.conservation());
        assert(!m.commit(*turn, 99) && !m.panic(*turn));
    }
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
    cross_tenant_same_actor();
    concurrent_results();
    concurrent_terminal_races();
    concurrent_close_with_queued_mail();
    panic_restart_close();
    std::puts("ATTO_FORGE_HOST_CLOSE_QUEUED_RACES_64=PASS");
    std::puts("ATTO_FORGE_HOST_TERMINAL_RACES_64=PASS");
    std::puts("ATTO_FORGE_HOST_PER_MAIL_TICKETS_128_CONCURRENT=PASS");
    std::puts("ATTO_FORGE_SCOPE=HOST_ONLY_NO_COMPILED_ATTO_PROXY");
}
