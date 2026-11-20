#pragma once
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace oresforge::runtime {
// Host-side accounting only. Native Forge machine code is NOT sandboxed.
enum class IsolatePhase { Created, Running, Quiescing, Stopped };
class OresVM final {
  struct State final {
    explicit State(std::string t, std::uint64_t g, std::size_t max_actors)
      : tenant(std::move(t)), generation(g), actor_limit(max_actors) {}
    std::string tenant;
    std::uint64_t generation;
    std::size_t actor_limit;
    IsolatePhase phase = IsolatePhase::Created;
    std::unordered_map<std::uint64_t, std::uint64_t> actors;
    std::unordered_map<std::uint64_t, std::size_t> actor_pins;
    std::uint64_t next_actor_epoch = 1;
    std::size_t pinned_operations = 0;
    std::size_t active_guest_calls = 0;
    std::int64_t scalar_state = 0;
    std::mutex guest_turn_mutex;
  };
public:
  class IsolateHandle final {
    friend class OresVM;
    std::weak_ptr<State> state_;
    std::uint64_t epoch_ = 0;
    explicit IsolateHandle(std::shared_ptr<State> s) : state_(s), epoch_(s->generation) {}
  public: IsolateHandle()=default;
  };
  // Canonical opaque per-tenant generation handle. No new trust mode.
  using TenantVMHandle = IsolateHandle;
  class ActorTicket final {
    friend class OresVM;
    std::weak_ptr<State> state_;
    std::uint64_t isolate_epoch_=0, actor_id_=0, actor_epoch_=0;
    ActorTicket(const std::shared_ptr<State>& s,std::uint64_t a,std::uint64_t e):
      state_(s),isolate_epoch_(s->generation),actor_id_(a),actor_epoch_(e){}
  public: ActorTicket()=default;
  };
  class OperationPin final {
    friend class OresVM;
    std::shared_ptr<State> owner_;
    std::uint64_t actor_id_=0,actor_epoch_=0;
    OresVM* vm_=nullptr;
    bool released_=false;
    OperationPin(OresVM* vm,std::shared_ptr<State> state,std::uint64_t aid,std::uint64_t epoch):
      owner_(std::move(state)),actor_id_(aid),actor_epoch_(epoch),vm_(vm){}
  public:
    OperationPin(const OperationPin&)=delete;
    OperationPin& operator=(const OperationPin&)=delete;
    OperationPin(OperationPin&&)=delete;
    OperationPin& operator=(OperationPin&&)=delete;
    ~OperationPin()=default; // MUST NOT automatically release physical I/O leases.
  };
  explicit OresVM(std::size_t max_tenants=32):max_tenants_(max_tenants){
    if(max_tenants_==0||max_tenants_>1024)throw std::invalid_argument("tenant quota");
  }
  OresVM(const OresVM&)=delete;OresVM& operator=(const OresVM&)=delete;
  // Canonical name for the existing TRUSTED host admission only.
  // Native Forge machine code remains unsandboxed.
  TenantVMHandle create_tenant_vm(std::string tenant,std::size_t max_actors){
    return create_isolate(std::move(tenant),max_actors);
  }
  IsolateHandle create_isolate(std::string tenant,std::size_t max_actors){
    const bool invalid_tenant = tenant.empty() || tenant=="." || tenant==".." || tenant.size()>128 ||
      !std::all_of(tenant.begin(),tenant.end(),[](unsigned char c){
        return (c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
               (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.';
      });
    if(invalid_tenant||max_actors==0||max_actors>65536)
      throw std::invalid_argument("tenant identity or actor quota");
    std::lock_guard lock(mu_);
    if(states_.count(tenant))throw std::logic_error("duplicate tenant");
    if(states_.size()>=max_tenants_)throw std::logic_error("tenant quota exhausted");
    if(next_epoch_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("epoch");
    auto s=std::make_shared<State>(std::move(tenant),next_epoch_++,max_actors);
    states_.emplace(s->tenant,s);
    return IsolateHandle{s};
  }
  void start(const IsolateHandle& h){std::lock_guard lock(mu_);auto s=checked(h);
    if(s->phase!=IsolatePhase::Created)throw std::logic_error("start phase");
    s->phase=IsolatePhase::Running;}
  ActorTicket register_actor(const IsolateHandle& h,std::uint64_t id){
    std::lock_guard lock(mu_);auto s=checked(h);
    if(s->phase!=IsolatePhase::Running||id==0||s->actors.count(id)||s->actors.size()>=s->actor_limit)
      throw std::logic_error("actor admission denied");
    if(s->next_actor_epoch==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("actor epoch");
    auto e=s->next_actor_epoch++;s->actors.emplace(id,e);s->actor_pins.emplace(id,0);return ActorTicket{s,id,e};
  }
  std::unique_ptr<OperationPin> pin_operation(const IsolateHandle& h,const ActorTicket& a){
    std::lock_guard lock(mu_);auto s=checked(h);check_actor(s,a);
    if(s->phase!=IsolatePhase::Running||s->pinned_operations>=65536)
      throw std::logic_error("operation admission denied");
    auto token=std::unique_ptr<OperationPin>(new OperationPin(this,s,a.actor_id_,a.actor_epoch_));
    ++s->pinned_operations;++s->actor_pins.at(a.actor_id_);return token;
  }
  // Must be invoked by the trusted owner AFTER callback/operation deregistration.
  void retire_operation(const IsolateHandle& h,const ActorTicket& a,OperationPin& pin){
    std::lock_guard lock(mu_);auto s=checked(h);check_actor(s,a);
    if(pin.released_||pin.owner_!=s||pin.vm_!=this||pin.actor_id_!=a.actor_id_||pin.actor_epoch_!=a.actor_epoch_)
      throw std::logic_error("foreign, stale or replayed operation");
    pin.released_=true;--s->pinned_operations;--s->actor_pins.at(a.actor_id_);
  }
  void retire_actor(const IsolateHandle& h,const ActorTicket& a){
    std::lock_guard lock(mu_);auto s=checked(h);check_actor(s,a);
    if(s->actor_pins.at(a.actor_id_)!=0)throw std::logic_error("outstanding actor operation");
    s->actors.erase(a.actor_id_);s->actor_pins.erase(a.actor_id_);
  }
  // Trusted scalar source-to-native path. The emitted Forge symbol takes
  // (old, delta) and returns i64. It gets NO OresVM pointer and uses one
  // immutable native code body for independently mutable tenant state.
  // Overflow in this stage-0 Forge ABI *traps*; use only trusted input here.
  // Production error/status ABI and hostile tenant sandbox remain TODO.
  std::int64_t invoke_trusted_scalar(const IsolateHandle& h,
                                     std::int64_t(*entry)(std::int64_t,std::int64_t),
                                     std::int64_t delta) {
    if (!entry) throw std::invalid_argument("null compiled guest entry");
    static thread_local bool in_guest_turn = false;
    if (in_guest_turn) throw std::logic_error("nested native entry forbidden");
    struct EntryGuard {bool& active; ~EntryGuard(){active=false;}};
    in_guest_turn=true;
    EntryGuard thread_guard{in_guest_turn};
    std::shared_ptr<State> s;
    {
      std::lock_guard lock(mu_);
      s=checked(h);
      if(s->phase!=IsolatePhase::Running)
        throw std::logic_error("native guest admission closed");
      if(s->active_guest_calls>=64)
        throw std::logic_error("native guest quota exhausted");
      ++s->active_guest_calls;
    }
    struct CallPin {
      OresVM& vm;
      std::shared_ptr<State> state;
      ~CallPin(){std::lock_guard lock(vm.mu_);--state->active_guest_calls;}
    } pin{*this,s};
    std::unique_lock turn(s->guest_turn_mutex);
    std::int64_t old=0;
    {
      std::lock_guard lock(mu_);
      if(s->phase!=IsolatePhase::Running)
        throw std::logic_error("queued native guest cancelled on shutdown");
      old=s->scalar_state;
    }
    // This stage-0 trusted profile is explicitly old + delta. Its existing
    // Forge assembly traps on overflow (ud2), which would kill the host.
    // Reject before native entry, under the serial guest turn, so overflow
    // cannot escape tenant status handling as a host-process crash.
    // The general compiler needs a versioned status/out-value ABI instead.
    if ((delta>0 && old>std::numeric_limits<std::int64_t>::max()-delta) ||
        (delta<0 && old<std::numeric_limits<std::int64_t>::min()-delta))
      throw std::overflow_error("trusted scalar add would trap on overflow");
    // Do not hold the root registry mutex while executing guest machine code.
    const auto next=entry(old,delta);
    {
      std::lock_guard lock(mu_);
      if(s->phase!=IsolatePhase::Running && s->phase!=IsolatePhase::Quiescing)
        throw std::logic_error("native guest cannot commit to stopped isolate");
      s->scalar_state=next;
    }
    return next;
  }
  void begin_shutdown(const IsolateHandle& h){std::lock_guard lock(mu_);auto s=checked(h);
    if(s->phase!=IsolatePhase::Running&&s->phase!=IsolatePhase::Created)throw std::logic_error("shutdown phase");
    s->phase=IsolatePhase::Quiescing;}
  void complete_shutdown(const IsolateHandle& h){std::lock_guard lock(mu_);auto s=checked(h);
    if(s->phase!=IsolatePhase::Quiescing||!s->actors.empty()||s->pinned_operations!=0||s->active_guest_calls!=0)
      throw std::logic_error("isolate has live operations or actors");
    s->phase=IsolatePhase::Stopped;}
  void destroy(const IsolateHandle& h){std::lock_guard lock(mu_);auto s=checked(h);
    if(s->phase!=IsolatePhase::Stopped)throw std::logic_error("must stop");
    states_.erase(s->tenant);}
  struct Snapshot{IsolatePhase phase;std::size_t actors,operations;std::uint64_t generation;std::int64_t scalar_state;std::size_t active_guest_calls;};
  Snapshot snapshot(const IsolateHandle& h) const{std::lock_guard lock(mu_);auto s=checked(h);
    return{s->phase,s->actors.size(),s->pinned_operations,s->generation,s->scalar_state,s->active_guest_calls};}
private:
  std::shared_ptr<State> checked(const IsolateHandle& h) const{
    auto s=h.state_.lock();if(!s||s->generation!=h.epoch_)throw std::logic_error("expired handle");
    auto it=states_.find(s->tenant);if(it==states_.end()||it->second!=s)throw std::logic_error("foreign isolate");
    return s;
  }
  static void check_actor(const std::shared_ptr<State>& s,const ActorTicket& a){
    auto it=s->actors.find(a.actor_id_);
    if(a.state_.lock()!=s||a.isolate_epoch_!=s->generation||it==s->actors.end()||it->second!=a.actor_epoch_)
      throw std::logic_error("stale or foreign actor");
  }
  mutable std::mutex mu_;
  std::unordered_map<std::string,std::shared_ptr<State>> states_;
  const std::size_t max_tenants_;
  std::uint64_t next_epoch_=1;
};
}
