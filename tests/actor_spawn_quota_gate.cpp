#include "runtime/actor_spawn_quota_gate.hpp"
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>
using Gate=oresforge::runtime::ActorSpawnQuotaGate<2,2>;
static void check(bool b){if(!b)std::abort();}
template<class F> void deny(F f,const char* code){
    try{f();}catch(const std::logic_error& e){check(std::string(e.what())==code);return;}
    std::abort();
}
int main(){
    const Gate::Charge unit{1,1,1,1};
    Gate g(unit),foreign(unit);
    auto p=g.reserve(unit);
    check(!g.try_enter_receive(p));
    deny([&]{foreign.begin_init(p);},"E_SPAWN_STALE_PERMIT");
    auto init=g.begin_init(p);
    deny([&]{g.begin_init(p);},"E_SPAWN_INIT_AUTHORITY");
    check(g.complete_init(init,true));
    deny([&]{g.complete_init(init,true);},"E_SPAWN_INIT_AUTHORITY");
    auto a=g.try_enter_receive(p).value(),b=g.try_enter_receive(p).value();
    deny([&]{g.try_enter_receive(p);},"E_SPAWN_FULL");
    deny([&]{g.release_after_quiescence(p);},"E_SPAWN_NOT_RETIRED");
    g.retire(p);check(!g.try_enter_receive(p));
    deny([&]{g.release_after_quiescence(p);},"E_SPAWN_IN_FLIGHT");
    g.finish_receive(a);
    deny([&]{g.finish_receive(a);},"E_SPAWN_STALE_PERMIT");
    deny([&]{g.release_after_quiescence(p);},"E_SPAWN_IN_FLIGHT");
    g.finish_receive(b);g.release_after_quiescence(p);
    auto q=g.reserve(unit);check(q.generation()>p.generation());
    deny([&]{g.retire(p);},"E_SPAWN_STALE_PERMIT");
    auto qi=g.begin_init(q);g.retire(q);
    deny([&]{g.release_after_quiescence(q);},"E_SPAWN_IN_FLIGHT");
    check(!g.complete_init(qi,true));g.release_after_quiescence(q);
    for(std::size_t i=0;i<4;++i){auto c=unit;c[i]=2;deny([&]{g.reserve(c);},"E_SPAWN_QUOTA");}
    auto m=std::numeric_limits<std::uint64_t>::max();
    Gate max({m,m,m,m});auto big=max.reserve({m,m,m,m});
    deny([&]{max.reserve(unit);},"E_SPAWN_QUOTA");
    max.retire(big);max.release_after_quiescence(big);
    for(int i=0;i<100000;++i){auto x=g.reserve(unit);check(!g.complete_init(g.begin_init(x),false));g.release_after_quiescence(x);}
    check(g.usage()==Gate::Charge{} && max.usage()==Gate::Charge{});
    std::barrier start(16),attempted(16);
    std::atomic<int> winners=0;
    std::vector<std::thread> threads;
    for(int i=0;i<16;++i)threads.emplace_back([&]{
        start.arrive_and_wait();
        std::optional<Gate::Permit> permit;
        try{permit=g.reserve(unit);++winners;}
        catch(const std::logic_error& e){check(std::string(e.what())=="E_SPAWN_QUOTA");}
        attempted.arrive_and_wait();
        if(permit){g.retire(*permit);g.release_after_quiescence(*permit);}
    });
    for(auto& t:threads)t.join();
    check(winners==1 && g.usage()==Gate::Charge{});
    std::cout<<"FORGE_SPAWN_QUOTA_HOST_PASS rollback=100000 contenders=16 guest=0\n";
}
