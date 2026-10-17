#include "compiler/dop_owned_f32_v1.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <atomic>
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
    std::cout << "PASS "<<checks<<" owned F32 assertions\n";
}
