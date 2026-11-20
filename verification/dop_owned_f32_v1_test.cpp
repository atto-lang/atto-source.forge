#include "compiler/dop_owned_f32_v1.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <type_traits>
static_assert(std::is_copy_constructible_v<oresforge::dop_v1::HostBuffer::Area>);
static_assert(std::is_move_constructible_v<oresforge::dop_v1::HostBuffer::Area>);
static_assert(!std::is_copy_assignable_v<oresforge::dop_v1::HostBuffer::Area>,
              "sealed DOP area capability must not be rebound after validation");
static_assert(!std::is_move_assignable_v<oresforge::dop_v1::HostBuffer::Area>);
using namespace oresforge::dop_v1;
static std::size_t checks;
static void check(bool v) { ++checks; if (!v) throw std::runtime_error("owned F32 assertion"); }
template <class Fn> static void rejects(Fn f) { ++checks; try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("admitted invalid owned F32 operation"); }
int main() {
    for (auto layout : {Layout::AoS, Layout::SoA, Layout::AoSoA}) {
        Shape shape{layout, 17, 4, layout == Layout::AoSoA ? 4u : 1u};
        HostBuffer a(shape), b(shape);
        for (unsigned row=0;row<17;++row) for(unsigned field=0;field<4;++field)
            a.write(row,field,static_cast<float>(row*4+field));
        auto src=a.area(Mode::Read,2,3,11), dst=b.area(Mode::Write,2,3,11);
        // A copied capability retains its original owner/privilege/bounds;
        // it cannot be rebound to an unrelated allocation between preflight
        // and actual loan acquisition.
        const HostBuffer::Area sealedSource(src);
        const HostBuffer::Area sealedDestination(dst);
        check(a.owns_area(sealedSource, Mode::Read));
        check(b.owns_area(sealedDestination, Mode::Write));
        rejects([&]{ execute_cpu_region(b,a,sealedSource,sealedDestination,1,0); });
        execute_cpu_region(a,b,src,dst,2.0f,1.0f);
        for (unsigned row=0;row<17;++row) for(unsigned field=0;field<4;++field)
            check(b.read(row,field)==(field==2&&row>=3&&row<11?2.0f*(row*4+field)+1.0f:0.0f));
        rejects([&]{execute_cpu_region(a,b,a.area(Mode::Write,2,3,11),dst,1,0);});
        rejects([&]{execute_cpu_region(a,b,src,b.area(Mode::Read,2,3,11),1,0);});
        rejects([&]{execute_cpu_region(a,b,src,b.area(Mode::Write,2,3,12),1,0);});
        rejects([&]{execute_cpu_region(b,b,b.area(Mode::Read,2,3,11),dst,1,0);});
        rejects([&]{execute_cpu_region(a,b,src,dst,std::numeric_limits<float>::quiet_NaN(),0);});
        rejects([&]{a.area(Mode::Read,2,0,18);});
        rejects([&]{a.area(Mode::Read,4,0,17);});
        {
            auto hold=b.borrow_read();
            rejects([&]{execute_cpu_region(a,b,src,dst,1,0);});
            rejects([&]{b.close();});
            rejects([&]{b.write(0,0,3);});
            check(hold.read(4,2)==(2*(4*4+2)+1));
        }
        {
            auto hold=b.borrow_write();
            rejects([&]{b.borrow_read();});
            rejects([&]{b.close();});
        }
        execute_cpu_region(a,b,a.area(Mode::Read,0,0,0),b.area(Mode::Write,0,0,0),1,0);
        b.close();
        rejects([&]{b.area(Mode::Read,2,3,11);});
        rejects([&]{b.borrow_read();});
        rejects([&]{execute_cpu_region(a,b,src,dst,1,0);});
    }
    rejects([]{HostBuffer b(Shape{Layout::AoSoA,17,4,3});});
    rejects([]{HostBuffer b(Shape{Layout::AoS,17,0,1});});
    rejects([]{HostBuffer b(Shape{Layout::AoS,std::numeric_limits<std::uint64_t>::max(),4,1});});
    rejects([]{HostBuffer b(Shape{Layout::AoS,100000000,4,1});});
    // Repeated shape/half-open-slice parity checks for all three physical layouts.
    // Use a deterministic generator, so any offending seed is reproducible.
    std::uint32_t seed = 0xD0F2026u;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    for (int trial=0; trial<450; ++trial) {
        auto layout = trial % 3 == 0 ? Layout::AoS : (trial % 3 == 1 ? Layout::SoA : Layout::AoSoA);
        auto rows = static_cast<std::uint64_t>(rnd() % 40);
        auto fields = static_cast<std::uint32_t>(1 + rnd() % 5);
        Shape shape{layout,rows,fields,layout==Layout::AoSoA ? 4u : 1u};
        HostBuffer in(shape), out(shape);
        auto field = static_cast<std::uint32_t>(rnd() % fields);
        const auto begin = rows == 0 ? 0u : rnd() % (rows+1);
        const auto end = begin + (rows == begin ? 0u : rnd() % (rows-begin+1));
        for(std::uint64_t i=0;i<rows;++i)for(std::uint32_t f=0;f<fields;++f)
            in.write(i,f,static_cast<float>(3*i+f));
        execute_cpu_region(in,out,in.area(Mode::Read,field,begin,end),
            out.area(Mode::Write,field,begin,end),3.0f,-2.0f);
        for(std::uint64_t i=0;i<rows;++i)for(std::uint32_t f=0;f<fields;++f)
            check(out.read(i,f)==(f==field && i>=begin && i<end ? 3.0f*(3*i+f)-2.0f : 0.0f));
    }
    // A conflicting writer running on another native thread must not
    // slip through a whole-buffer read loan.
    {
        HostBuffer b(Shape{Layout::AoS, 8, 2, 1});
        auto borrowed = b.borrow_read();
        std::atomic<bool> denied{false};
        std::thread writer([&] {
            try { b.write(0,0,42.0f); }
            catch(const std::logic_error&) { denied.store(true); }
        });
        writer.join();
        check(denied.load());
        check(borrowed.read(0,0) == 0.0f);
    }
    std::cout << "PASS "<<checks<<" owned F32 assertions\n";
}
