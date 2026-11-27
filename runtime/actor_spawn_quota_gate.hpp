#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace oresforge::runtime {
// Host-only quota substrate. Trusted dispatcher must certify actual
// heap, stack, task, mailbox and FFI quiescence before refund. No guest ABI.
template<std::size_t N, std::size_t R>
class ActorSpawnQuotaGate final {
    static_assert(N > 0 && R > 0);
    struct Identity {};
public:
    using Charge=std::array<std::uint64_t,4>;
    class Permit {
        friend class ActorSpawnQuotaGate;
        std::shared_ptr<const Identity> owner_;
        std::size_t slot_;
        std::uint64_t generation_;
        Permit(std::shared_ptr<const Identity> owner,std::size_t slot,std::uint64_t generation)
            :owner_(std::move(owner)),slot_(slot),generation_(generation){}
    public:
        std::uint64_t generation() const noexcept {return generation_;}
    };
    class InitLease {
        friend class ActorSpawnQuotaGate;
        Permit p_;
        explicit InitLease(Permit p):p_(std::move(p)){}
    };
    class ReceiveLease {
        friend class ActorSpawnQuotaGate;
        Permit p_;
        std::uint64_t nonce_;
        ReceiveLease(Permit p,std::uint64_t nonce):p_(std::move(p)),nonce_(nonce){}
    };
    explicit ActorSpawnQuotaGate(Charge limit):limit_(limit) {
        for(auto x:limit) if(!x) deny("E_SPAWN_QUOTA");
    }
    Permit reserve(Charge charge) {
        std::lock_guard lock(mutex_);
        for(std::size_t i=0;i<4;++i)
            if(!charge[i] || charge[i]>limit_[i]-used_[i]) deny("E_SPAWN_QUOTA");
        std::size_t slot=0;
        while(slot<N && entries_[slot].generation) ++slot;
        if(slot==N) deny("E_SPAWN_FULL");
        if(generation_==std::numeric_limits<std::uint64_t>::max()) deny("E_SPAWN_GENERATION");
        auto gen=++generation_;
        entries_[slot]=Entry{charge,gen};
        for(std::size_t i=0;i<4;++i) used_[i]+=charge[i];
        return Permit(identity_,slot,gen);
    }
    InitLease begin_init(const Permit& p) {
        std::lock_guard lock(mutex_);
        auto& e=require(p);
        if(e.phase!=Phase::initializing || e.init_started) deny("E_SPAWN_INIT_AUTHORITY");
        e.init_started=e.init_live=true;
        return InitLease(p);
    }
    bool complete_init(const InitLease& lease,bool success) {
        std::lock_guard lock(mutex_);
        auto& e=require(lease.p_);
        if(!e.init_live) deny("E_SPAWN_INIT_AUTHORITY");
        e.init_live=false;
        if(!success || e.phase==Phase::retiring) {e.phase=Phase::retiring;return false;}
        e.phase=Phase::ready;return true;
    }
    std::optional<ReceiveLease> try_enter_receive(const Permit& p) {
        std::lock_guard lock(mutex_);
        auto& e=require(p);
        if(e.phase!=Phase::ready) return {};
        for(auto& nonce:e.receives) if(!nonce) {
            if(e.next_receive==std::numeric_limits<std::uint64_t>::max()) deny("E_SPAWN_LEASE_LIMIT");
            nonce=++e.next_receive;return ReceiveLease(p,nonce);
        }
        deny("E_SPAWN_FULL");
    }
    void finish_receive(const ReceiveLease& lease) {
        std::lock_guard lock(mutex_);
        auto& e=require(lease.p_);
        for(auto& nonce:e.receives) if(nonce && nonce==lease.nonce_) {nonce=0;return;}
        deny("E_SPAWN_STALE_PERMIT");
    }
    void retire(const Permit& p) {
        std::lock_guard lock(mutex_); require(p).phase=Phase::retiring;
    }
    // Only the host scheduler may call this after actual physical cleanup.
    void release_after_quiescence(const Permit& p) {
        std::lock_guard lock(mutex_);
        auto& e=require(p);
        if(e.phase!=Phase::retiring) deny("E_SPAWN_NOT_RETIRED");
        if(e.init_live) deny("E_SPAWN_IN_FLIGHT");
        for(auto nonce:e.receives) if(nonce) deny("E_SPAWN_IN_FLIGHT");
        for(std::size_t i=0;i<4;++i) used_[i]-=e.charge[i];
        e=Entry{};
    }
    Charge usage() const {std::lock_guard lock(mutex_);return used_;}
private:
    enum class Phase {initializing,ready,retiring};
    struct Entry {
        Charge charge{};
        std::uint64_t generation=0;
        Phase phase=Phase::initializing;
        bool init_started=false,init_live=false;
        std::array<std::uint64_t,R> receives{};
        std::uint64_t next_receive=0;
    };
    mutable std::mutex mutex_;
    const std::shared_ptr<const Identity> identity_=std::make_shared<const Identity>();
    const Charge limit_;
    Charge used_{};
    std::uint64_t generation_=0;
    std::array<Entry,N> entries_{};
    [[noreturn]] static void deny(const char* code) {throw std::logic_error(code);}
    Entry& require(const Permit& p) {
        if(p.owner_!=identity_ || p.slot_>=N || !entries_[p.slot_].generation ||
           entries_[p.slot_].generation!=p.generation_) deny("E_SPAWN_STALE_PERMIT");
        return entries_[p.slot_];
    }
};
}
