#include "compiler/dop_owned_graph_v1.hpp"
#include <atomic>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace oresforge::dop_v1;
namespace graph = oresforge::execution_v1;
static std::size_t checks;
static void check(bool ok, const char* why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}
template<class Fn> static void reject(Fn&& fn, const char* why) {
    ++checks;
    try { fn(); } catch (const std::exception&) { return; }
    throw std::runtime_error(why);
}
static graph::Plan answer() {
    graph::Builder b;
    auto left = b.cpu_i64(20), right = b.cpu_i64(22);
    return b.build(b.cpu_add(left, right));
}
int main() {
    for (auto layout : {Layout::AoS, Layout::SoA, Layout::AoSoA}) {
        const Shape shape{layout, 9, 3, layout == Layout::AoSoA ? 4u : 1u};
        auto in = std::make_shared<HostBuffer>(shape);
        auto out = std::make_shared<HostBuffer>(shape);
        for (unsigned row=0; row<9; ++row)
            for (unsigned col=0; col<3; ++col)
                in->write(row,col,static_cast<float>(row*3+col));
        const auto src = in->area(Mode::Read,1,2,6);
        const auto dst = out->area(Mode::Write,1,2,6);
        std::function<void()> queued;
        auto schedule = [&](std::function<void()> task) {queued=std::move(task);};

        auto cancelled = submit_owned_graph(in,out,src,dst,2.0f,1.0f,answer(),schedule);
        check(!cancelled.ready() && bool(queued),"pending bridge task missing");
        check(cancelled.cancel(),"preclaim cancellation must work");
        queued();
        reject([&]{(void)cancelled.get();},"cancelled future published");
        check(out->read(2,1)==0,"cancelled CPU buffer mutated");
        auto good = submit_owned_graph(in,out,src,dst,2,1,answer(),schedule);
        queued();
        queued(); // erroneous duplicate callback
        check(good.get()==42,"I64 graph result wrong");
        check(!good.cancel(),"completed task accepted cancellation");
        for (unsigned row=0;row<9;++row)
            for(unsigned col=0;col<3;++col) {
                const float expected=col==1&&row>=2&&row<6?
                    (2.0f*(row*3+col)+1.0f):0.0f;
                check(out->read(row,col)==expected,"F32 row/field mismatch");
            }
        queued={};
        graph::Builder gpu;
        auto device=gpu.upload_i64(gpu.cpu_i64(1));
        auto forbidden=submit_owned_graph(in,out,src,dst,1,0,gpu.build(device),schedule);
        reject([&]{(void)forbidden.get();},"GPU graph fell back");
        check(!queued,"GPU graph scheduled CPU task");
        auto invalid = submit_owned_graph(out,in,src,dst,1,0,answer(),schedule);
        reject([&]{(void)invalid.get();},"wrong-owner area admitted");
        check(!queued,"wrong-owner area was scheduled");

        {
            auto read_loan=out->borrow_read();
            auto raced=submit_owned_graph(in,out,src,dst,1,0,answer(),schedule);
            queued();
            reject([&]{(void)raced.get();},"borrowed destination illegally overwritten");
            check(read_loan.read(2,1)==15,"failed loan mutated bytes");
        }
        queued={};
        auto rejected=submit_owned_graph(in,out,src,dst,1,0,answer(),
            [&](std::function<void()> f) {
                queued=std::move(f);
                throw std::runtime_error("capacity exceeded after enqueue");
            });
        reject([&]{(void)rejected.get();},"scheduler throw failed to settle");
        check(bool(queued),"scheduler didn't retain callback");
        out->write(2,1,123);
        queued();
        check(out->read(2,1)==123,"late rejected callback wrote F32 storage");

        auto inline_result=submit_owned_graph(in,out,src,dst,1,0,answer(),
            [](std::function<void()> task) {task();});
        reject([&]{(void)inline_result.get();},"inline executor admitted");
        auto inline_throw=submit_owned_graph(in,out,src,dst,1,0,answer(),
            [](std::function<void()> task) {
                task();
                throw std::runtime_error("actual inline scheduler throw");
            });
        try {
            (void)inline_throw.get();
            throw std::runtime_error("inline throw incorrectly succeeded");
        } catch (const std::runtime_error& ex) {
            check(std::string(ex.what())=="actual inline scheduler throw",
                "inline rejection masked scheduling failure");
        }
        for(int trial=0; trial<64; ++trial) {
            std::promise<void> launched;
            auto launched_future=launched.get_future();
            std::thread early;
            auto accepted_early=submit_owned_graph(in,out,src,dst,1,0,answer(),
                [&](std::function<void()> task) {
                    early=std::thread([job=std::move(task),&launched]() mutable {
                        launched.set_value();
                        job();
                    });
                    launched_future.wait();
                    // Normal scheduler return commits admission.
                });
            early.join();
            check(accepted_early.get()==42,
                "early worker failed after accepted scheduler admission");
        }
        for(int trial=0; trial<64; ++trial) {
            std::promise<void> launched;
            auto launched_future=launched.get_future();
            std::thread early;
            auto rejected_early=submit_owned_graph(in,out,src,dst,1,0,answer(),
                [&](std::function<void()> task) {
                    early=std::thread([job=std::move(task),&launched]() mutable {
                        launched.set_value();
                        job();
                    });
                    launched_future.wait();
                    throw std::runtime_error("owned worker began before rejection");
                });
            early.join();
            try {
                (void)rejected_early.get();
                throw std::runtime_error("started rejected worker unexpectedly succeeded");
            } catch(const std::runtime_error& ex) {
                check(std::string(ex.what())=="owned worker began before rejection",
                    "started-worker scheduler rejection was masked");
            }
        }

        for(int trial=0; trial<64; ++trial) {
            auto once=submit_owned_graph(in,out,src,dst,1,0,answer(),schedule);
            std::thread t1([&]{queued();}),t2([&]{queued();});
            t1.join();t2.join();
            check(once.get()==42,"concurrent duplicate task changed result");
        }
        for(int trial=0; trial<64; ++trial) {
            auto once=submit_owned_graph(in,out,src,dst,1,0,answer(),schedule);
            std::thread worker([&]{queued();});
            (void)once.cancel();
            worker.join();
            check(once.ready(),"cancel/claim left future pending");
            try { check(once.get()==42,"unexpected successful I64 result"); }
            catch(const graph::Cancellation&) {check(once.ready(),"cancel not terminal");}
        }
        // Close after structural preflight must be revalidated at physical
        // loan admission. No queued callback may revive closed storage.
        auto close_out=std::make_shared<HostBuffer>(shape);
        auto close_dst=close_out->area(Mode::Write,1,2,6);
        queued={};
        auto close_race=submit_owned_graph(in,close_out,src,close_dst,1,0,answer(),schedule);
        auto close_task=queued;
        close_out->close();
        close_task();
        reject([&]{(void)close_race.get();},"closed allocation executed after preflight");

        queued={};
        auto retained=submit_owned_graph(in,out,src,dst,1,0,answer(),schedule);
        std::weak_ptr<HostBuffer> wi=in,wo=out;
        in.reset();out.reset();
        check(!wi.expired()&&!wo.expired(),"queued work lost strong owners");
        queued();
        check(retained.get()==42,"retained owners execution failed");
        queued={};
        check(wi.expired()&&wo.expired(),"completed job leaked buffer ownership");
    }
    std::cout << "PASS " << checks << " Forge owned-F32/I64 graph assertions\n";
}
