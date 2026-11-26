#include "atto/durable_bounded_emit_v2.hpp"
#include <cassert>
#include <fstream>
using C=ores::durable::BoundedCheckpointV2;
using K=C::Kind;
using atto::forge::emit_durable_bounded_v2;
int main(int argc,char** argv){
    assert(argc==2);
    const std::vector<C::Field> fields{{"count",K::Int},{"open",K::Bool},{"name",K::String},{"payload",K::Bytes}};
    const auto text=emit_durable_bounded_v2("Ledger",fields);
    assert(text==emit_durable_bounded_v2("Ledger",fields));
    auto reject=[](auto f){bool denied=false;try{f();}catch(const std::logic_error&){denied=true;}assert(denied);};
    reject([&]{(void)emit_durable_bounded_v2("Bad\";",fields);});
    reject([&]{(void)emit_durable_bounded_v2("Ledger",{{"x",K::Int},{"x",K::Bool}});});
    reject([&]{(void)emit_durable_bounded_v2("Ledger",{{"x",static_cast<K>(255)}});});
    std::ofstream f(argv[1]);f<<text;f.close();assert(f.good());
}
