#include "runtime/persistent_actor_map.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

using namespace oresforge::runtime;

int main() {
    auto empty = PersistentActorMap::empty();
    auto one = empty.with("Aa", 1);
    auto both = one.with("BB", 2);
    assert(empty.size() == 0 && one.size() == 1 && both.size() == 2);
    assert(std::get<std::int64_t>(one.get("Aa")->storage()) == 1);
    assert(std::get<std::int64_t>(both.get("BB")->storage()) == 2);

    auto old = both;
    auto changed = both.with("Aa", 99);
    assert(std::get<std::int64_t>(old.get("Aa")->storage()) == 1);
    assert(std::get<std::int64_t>(changed.get("Aa")->storage()) == 99);

    auto nested = std::make_shared<const PersistentActorMap>(both);
    auto parent = empty.with("child", PersistentActorMap::Value(nested));
    assert(std::get<PersistentActorMap::MapRef>(
        parent.get("child")->storage()) == nested);
    assert(parent.with("child", PersistentActorMap::Value(nested)).size() == 1);

    auto deleted = changed.without("BB");
    assert(!deleted.contains("BB") && changed.contains("BB"));

    PersistentActorMap flood = empty;
    for (int n = 0; n < 64; ++n) {
        std::string key;
        for (int i = 0; i < 7; ++i)
            key += ((n >> i) & 1) ? "BB" : "Aa";
        flood = flood.with(key, n);
    }
    bool denied = false;
    try { (void)flood.with("AaAaAaAaAaAaBB", 65); }
    catch (const std::length_error&) { denied = true; }
    assert(denied);
}
