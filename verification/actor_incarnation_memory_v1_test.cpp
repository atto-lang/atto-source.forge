#include "runtime/ActorIncarnationMemoryV1.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace oresforge::runtime;
static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <typename F> static void denied(F&& fn, const char* message) {
  try { fn(); } catch (const std::exception&) { return; }
  throw std::runtime_error(message);
}
int main() {
  Mailbox mailboxA(9101), mailboxB(9102);
  const auto actorA = mailboxA.ref();
  const auto actorB = mailboxB.ref();
  ActorIncarnationMemoryV1 a(mailboxA, actorA, 256, 64, 4, 2);
  ActorIncarnationMemoryV1 b(mailboxB, actorB, 256, 64, 4, 2);

  const auto ha = a.alloc(actorA, 16);
  const auto hb = b.alloc(actorB, 16);
  std::array<std::uint8_t, 4> input{42, 9, 8, 7};
  a.heap_write(actorA, ha, 0, input);
  input[0] = 99;
  std::array<std::uint8_t, 4> output{};
  a.heap_read(actorA, ha, 0, output);
  require(output == (std::array<std::uint8_t,4>{42,9,8,7}),
          "actor heap must copy caller-owned bytes");
  b.heap_read(actorB, hb, 0, output);
  require(output == (std::array<std::uint8_t,4>{0,0,0,0}),
          "second actor has independent zeroed heap");
  denied([&]{ a.heap_read(actorB, ha, 0, output); },
         "foreign actor used first actor heap");
  denied([&]{ b.heap_read(actorB, ha, 0, output); },
         "foreign heap allocation token accepted");
  a.drop(actorA, ha);
  denied([&]{ a.heap_read(actorA, ha, 0, output); },
         "dropped heap token remained live");
  const auto ha2 = a.alloc(actorA, 16);
  a.heap_read(actorA, ha2, 0, output);
  require(output == (std::array<std::uint8_t,4>{0,0,0,0}),
          "reused actor heap bytes must be scrubbed");

  auto ea = a.begin(actorA);
  auto outerA = a.push(actorA, ea, 16);
  auto innerA = a.push(actorA, ea, 16);
  denied([&]{ a.pop(actorA, ea, outerA); },
         "wrong LIFO pop did not terminate actor A frames");
  require(a.poisoned() && !b.poisoned(),
          "one actor frame fault poisoned its healthy sibling");
  denied([&]{ a.frame_read(actorA, ea, innerA, 0, output); },
         "poisoned actor A read old stack bytes");
  denied([&]{ a.heap_read(actorA, ha2, 0, output); },
         "a terminal actor frame fault left heap operations runnable");

  auto eb = b.begin(actorB);
  auto fb = b.push(actorB, eb, 32);
  b.frame_write(actorB, eb, fb, 0, std::array<std::uint8_t,1>{77});
  [[maybe_unused]] auto suspended = b.suspend(actorB, eb);
  require(b.depth()==1, "suspend must preserve logical frame");
  denied([&]{ b.frame_read(actorB, eb, fb, 0, output); },
         "suspended actor frame was dereferenced");
  require(b.poisoned(), "attempted frame access while suspended must poison");
  // A fresh actor can still use a separate stack after B's local fault.
  Mailbox mailboxC(9103);
  const auto actorC = mailboxC.ref();
  ActorIncarnationMemoryV1 c(mailboxC, actorC, 256, 64, 4, 2);
  auto ec = c.begin(actorC);
  auto fc = c.push(actorC, ec, 24);
  c.frame_write(actorC, ec, fc, 0, std::array<std::uint8_t,1>{42});
  auto cc = c.suspend(actorC, ec);
  std::exception_ptr workerFailure;
  std::thread resumed([&] {
    try {
      c.resume(actorC, ec, cc);
      std::array<std::uint8_t,1> result{};
      c.frame_read(actorC, ec, fc, 0, result);
      require(result[0]==42, "migrated actor frame lost stored bytes");
      c.pop(actorC, ec, fc);
      c.finish(actorC, ec);
      c.verify_handler_exit(actorC);
    } catch (...) { workerFailure = std::current_exception(); }
  });
  resumed.join();
  if (workerFailure) std::rethrow_exception(workerFailure);
  require(c.depth()==0 && !c.poisoned(),
          "healthy actor C resumed and returned on distinct carrier");
  denied([&]{ c.resume(actorC, ec, cc); },
         "reused continuation was admitted");
  require(c.poisoned(), "replay of continuation must terminate local stack");

  Mailbox mailboxD(9104);
  const auto actorD = mailboxD.ref();
  ActorIncarnationMemoryV1 d(mailboxD, actorD, 128, 48, 2, 2);
  auto ed = d.begin(actorD);
  [[maybe_unused]] auto hd = d.push(actorD, ed, 32);
  denied([&]{ d.push(actorD, ed, 17); },
         "stack byte quota did not fail closed");
  require(d.poisoned(), "stack byte quota did not poison actor");

  // A host without a parked-actor scheduler must fail closed if a suspended
  // logical stack escapes a handler. It cannot admit a second actor turn.
  Mailbox mailboxE(9105);
  const auto actorE = mailboxE.ref();
  ActorIncarnationMemoryV1 e(mailboxE, actorE, 128, 48, 2, 2);
  auto ee = e.begin(actorE);
  [[maybe_unused]] auto fe = e.push(actorE, ee, 8);
  [[maybe_unused]] auto ce = e.suspend(actorE, ee);
  denied([&]{ e.verify_handler_exit(actorE); },
         "suspended Forge actor escaped its synchronous handler");
  require(e.poisoned(), "unsupported handler suspension must poison actor");
  denied([&]{ e.begin(actorE); },
         "suspended escape allowed the next message to start");

  // Process and runtime quotas are charged before native allocation. A
  // failed second admission must roll back its process reservation exactly.
  ActorMemoryAggregateBudgetV1 scopedBudget(176);
  Mailbox budgetMailboxA(9106), budgetMailboxB(9107);
  const auto budgetActorA = budgetMailboxA.ref();
  const auto budgetActorB = budgetMailboxB.ref();
  auto& processBudget = ActorMemoryAggregateBudgetV1::process();
  const std::size_t baseline = processBudget.reserved();
  {
    ActorIncarnationMemoryV1 admitted(
        budgetMailboxA, budgetActorA, 128, 48, 2, 2, scopedBudget);
    require(scopedBudget.reserved() == 176 &&
            processBudget.reserved() == baseline + 176,
            "actor admission must charge runtime and process budgets");
    bool quotaRejected = false;
    try {
      ActorIncarnationMemoryV1 deniedActor(
          budgetMailboxB, budgetActorB, 128, 48, 2, 2, scopedBudget);
    } catch (const std::length_error&) { quotaRejected = true; }
    require(quotaRejected, "concurrent actors exceeded aggregate native quota");
    require(scopedBudget.reserved() == 176 &&
            processBudget.reserved() == baseline + 176,
            "failed actor construction leaked aggregate native credits");
    admitted.retire();
    admitted.retire();
    require(scopedBudget.reserved() == 0 &&
            processBudget.reserved() == baseline,
            "native retirement must refund both budgets exactly once");
  }
  {
    ActorIncarnationMemoryV1 successorAfterRefund(
        budgetMailboxB, budgetActorB, 128, 48, 2, 2, scopedBudget);
    require(scopedBudget.reserved() == 176 &&
            processBudget.reserved() == baseline + 176,
            "actor admission failed after native quota refund");
  }
  require(scopedBudget.reserved() == 0 &&
          processBudget.reserved() == baseline,
          "destructor must refund quota for unretired actor memory");

  // The mailbox generation fence, not a matching numeric actor ID, is authority.
  // Deterministic restart/close linearization: an issued memory operation
  // holds mailbox generation authority until its guarded scope ends.
  std::atomic<bool> closeStarted{false};
  std::atomic<bool> closeFinished{false};
  bool closedBeforeLeaseExit = false;
  std::thread closeWorker;
  {
    auto lease = mailboxB.lock_authorized(actorB, actorB);
    closeWorker = std::thread([&] {
      closeStarted.store(true, std::memory_order_release);
      mailboxB.close();
      closeFinished.store(true, std::memory_order_release);
    });
    while (!closeStarted.load(std::memory_order_acquire))
      std::this_thread::yield();
    closedBeforeLeaseExit = closeFinished.load(std::memory_order_acquire);
  }
  closeWorker.join();
  require(!closedBeforeLeaseExit && closeFinished.load(std::memory_order_acquire),
          "STOP must wait for the issued mailbox-generation byte-operation lease");
  denied([&]{ b.heap_read(actorB, hb, 0, output); },
         "closed actor incarnation retained heap read authority");
  const auto restarted = mailboxB.restart();
  denied([&]{ b.heap_read(restarted, hb, 0, output); },
         "restarted actor inherited previous incarnation heap token");
  ActorIncarnationMemoryV1 successor(mailboxB, restarted, 128, 64, 2, 2);
  auto newH = successor.alloc(restarted, 8);
  std::array<std::uint8_t,1> blank{};
  successor.heap_read(restarted, newH, 0, blank);
  require(blank[0]==0, "successor heap is fresh, separate and zeroed");
  successor.drop(restarted, newH);
  b.retire();
  b.retire();
  denied([&]{ b.heap_read(actorB, hb, 0, output); },
         "retired heap accepted a stale read");

  std::cout << "FORGE_ACTOR_INCAR_MEMORY_HOST_PASS"
            << "|private_heap=1|logical_stack=1|guest_atto=0|physical_stack=0\n";
}
