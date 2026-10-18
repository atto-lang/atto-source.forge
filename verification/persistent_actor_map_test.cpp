#include "runtime/persistent_actor_map.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <limits>
#include <map>
#include <random>
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

    bool nonfiniteDenied = false;
    try { (void)empty.with("bad", PersistentActorMap::Value(
        std::numeric_limits<double>::infinity())); }
    catch (const std::invalid_argument&) { nonfiniteDenied = true; }
    assert(nonfiniteDenied);

    PersistentActorMap subject = empty;
    std::map<std::string, std::int64_t> oracle;
    std::mt19937_64 rng(0xF047Eu);
    for (int step = 0; step < 10000; ++step) {
        auto key = std::string("model-") + std::to_string(rng() % 1024);
        if ((rng() & 3u) == 0) {
            subject = subject.without(key);
            oracle.erase(key);
        } else {
            auto value = static_cast<std::int64_t>(rng() & 0x7fffffff);
            subject = subject.with(key, value);
            oracle[key] = value;
        }
        assert(subject.size() == oracle.size());
        for (int probe = 0; probe < 4; ++probe) {
            auto pkey = std::string("model-") + std::to_string(rng() % 1024);
            auto it = oracle.find(pkey);
            const auto* actual = subject.get(pkey);
            assert((it == oracle.end()) == (actual == nullptr));
            if (actual)
                assert(std::get<std::int64_t>(actual->storage()) == it->second);
        }
    }

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
