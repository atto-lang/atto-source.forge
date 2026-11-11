#include "runtime/DemandMailboxPort.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

using oresforge::runtime::DemandMailboxPort;

static void fifo_and_terminal() {
  DemandMailboxPort<1> port(100, 2, 16, 4);
  auto value = std::vector<std::uint8_t>{1,2};
  assert(port.try_offer(value) == decltype(port)::Offer::NoSubscriber);
  assert(value.size() == 2);
  assert(port.subscribe());
  assert(!port.subscribe());
  assert(port.try_offer(value) == decltype(port)::Offer::NoDemand);
  port.request(4);
  assert(port.try_offer(value) == decltype(port)::Offer::Accepted);
  assert(value.empty());
  auto next = std::vector<std::uint8_t>{3};
  assert(port.try_offer(next) == decltype(port)::Offer::Accepted);
  auto extra = std::vector<std::uint8_t>{4};
  assert(port.try_offer(extra) == decltype(port)::Offer::Full && extra.size()==1);
  auto first = port.try_next();
  assert(first.status == decltype(port)::PollStatus::Item &&
         first.value == (std::vector<std::uint8_t>{1,2}));
  assert(port.try_next().status == decltype(port)::PollStatus::NotReady);
  assert(port.complete());
  assert(!port.cancel());
  assert(port.queued() == 1);
  assert(port.ack(first.ticket) && !port.ack(first.ticket));
  auto second = port.try_next();
  assert(second.value == (std::vector<std::uint8_t>{3}));
  assert(port.ack(second.ticket));
  assert(port.try_next().status == decltype(port)::PollStatus::Complete);
  assert(port.retained_bytes() == 0);
}

static void cancel_scrubs_only_undelivered_payloads() {
  DemandMailboxPort<1> port(101,2,16,2);
  assert(port.subscribe());
  port.request(2);
  std::vector<std::uint8_t> a{1,2},b{3,4};
  assert(port.try_offer(a) == decltype(port)::Offer::Accepted);
  assert(port.try_offer(b) == decltype(port)::Offer::Accepted);
  auto checked_out = port.try_next();
  assert(port.cancel());
  assert(!port.complete());
  assert(port.queued() == 0);
  assert(port.retained_bytes() == 2);
  assert(port.try_next().status == decltype(port)::PollStatus::Cancelled);
  assert(port.ack(checked_out.ticket));
  assert(port.retained_bytes() == 0);
}

static void terminal_race_is_single_winner() {
  for (int i=0;i<100;++i) {
    DemandMailboxPort<1> port(1000+i,1,8,1);
    assert(port.subscribe());
    port.request(1);
    std::vector<std::uint8_t> source{9};
    assert(port.try_offer(source) == decltype(port)::Offer::Accepted);
    std::atomic<bool> start{false};
    bool closed=false,cancelled=false;
    std::thread a([&]{while(!start.load())std::this_thread::yield();closed=port.complete();});
    std::thread b([&]{while(!start.load())std::this_thread::yield();cancelled=port.cancel();});
    start=true;a.join();b.join();
    assert(closed != cancelled);
    if(closed){
      auto item=port.try_next();
      assert(item.status == decltype(port)::PollStatus::Item && item.value[0] == 9);
      assert(port.ack(item.ticket));
      assert(port.try_next().status == decltype(port)::PollStatus::Complete);
    } else assert(port.try_next().status == decltype(port)::PollStatus::Cancelled);
    assert(port.retained_bytes() == 0);
  }
}
int main() {fifo_and_terminal();cancel_scrubs_only_undelivered_payloads();terminal_race_is_single_winner();}
