#pragma once
#include "runtime/actor.hpp"
#include "runtime/singleton_cell.hpp"
#include "runtime/singleton_identity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace oresforge::shared {

enum class SingletonRegistryErrorCode {
  Denied,
  Capacity,
  Closed,
  InvalidDeclaration
};

class SingletonRegistryError final : public std::exception {
 public:
  const SingletonRegistryErrorCode code;
  explicit SingletonRegistryError(SingletonRegistryErrorCode value) : code(value) {}
  const char* what() const noexcept override {
    return "singleton registry operation failed";
  }
};

// Fixed-budget, application-domain registry. The registry owns declaration
// cells for its full lifetime: there is no hot-unload/reuse protocol in this
// slice. The compiler must resolve imports/aliases to SingletonDeclarationId
// before guest execution and the scheduler must supply ActorContext.
template<class T, std::size_t Capacity>
class SingletonRegistry final {
  static_assert(Capacity > 0);

  struct Slot {
    bool used = false;
    SingletonDeclarationId id{};
    SingletonCell<T> cell;
  };

  const std::uint64_t domain_;
  mutable std::mutex mutex_;
  bool closed_ = false;
  std::array<Slot, Capacity> slots_{};

  [[noreturn]] static void fail(SingletonRegistryErrorCode code) {
    throw SingletonRegistryError(code);
  }

  SingletonCell<T>* resolve(oresforge::runtime::ActorContext caller,
                            SingletonDeclarationId id) {
    std::scoped_lock lock(mutex_);
    if (!caller.valid() || caller.domain() != domain_)
      fail(SingletonRegistryErrorCode::Denied);
    if (!id.valid()) fail(SingletonRegistryErrorCode::InvalidDeclaration);
    if (closed_) fail(SingletonRegistryErrorCode::Closed);

    Slot* vacant = nullptr;
    for (auto& slot : slots_) {
      if (slot.used && slot.id == id) return &slot.cell;
      if (!slot.used && vacant == nullptr) vacant = &slot;
    }
    if (vacant == nullptr) fail(SingletonRegistryErrorCode::Capacity);
    vacant->id = id;
    vacant->used = true;
    return &vacant->cell;
  }

 public:
  explicit SingletonRegistry(std::uint64_t domain) : domain_(domain) {
    if (domain == 0) throw std::invalid_argument("invalid singleton registry domain");
  }
  SingletonRegistry(const SingletonRegistry&) = delete;
  SingletonRegistry& operator=(const SingletonRegistry&) = delete;

  std::uint64_t domain() const noexcept { return domain_; }

  template<class Initialize>
  std::shared_ptr<const T> get(
      oresforge::runtime::ActorContext caller,
      SingletonDeclarationId id,
      Initialize&& initialize) {
    // Slot addresses are stable for the registry lifetime. Never run an
    // initializer under the registry mutex: it may execute arbitrary host code.
    auto* cell = resolve(caller, id);
    return cell->get(std::forward<Initialize>(initialize));
  }

  // Runtime-bound path used by the stage-0 compiler bridge. The declaration ID
  // must come from compiler resolution, never guest input.
  template<class Initialize>
  std::shared_ptr<const T> get_active(
      SingletonDeclarationId id, Initialize&& initialize) {
    return get(oresforge::runtime::active_dispatch_actor(), id,
               std::forward<Initialize>(initialize));
  }

  void close() {
    std::array<SingletonCell<T>*, Capacity> cells{};
    std::size_t count = 0;
    {
      std::scoped_lock lock(mutex_);
      if (closed_) return;
      closed_ = true;
      for (auto& slot : slots_)
        if (slot.used) cells[count++] = &slot.cell;
    }
    // Cell close/destruction callbacks must never run under registry_mutex_.
    for (std::size_t i = 0; i < count; ++i) cells[i]->close();
  }
};

} // namespace oresforge::shared
