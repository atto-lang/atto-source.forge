#pragma once
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace oresforge::shared {
// Host substrate only. Guest lowering must prove deep immutability and prevent
// initializer receiver escape; shared_ptr<const T> alone cannot prove either.
inline thread_local unsigned singleton_initializer_depth = 0;
class SingletonClosed : public std::runtime_error {
 public: SingletonClosed() : std::runtime_error("singleton closed") {}
};
class SingletonInitializationDependency : public std::runtime_error {
 public: SingletonInitializationDependency()
     : std::runtime_error("nested singleton initialization requires dependency lowering") {}
};

template<class T> class SingletonCell final {
  enum class Phase { Cold, Initializing, Ready, Failed, Closed };
  std::mutex mutex_;
  std::condition_variable changed_;
  Phase phase_ = Phase::Cold;
  std::shared_ptr<const T> value_;
  std::exception_ptr failure_;
 public:
  SingletonCell() = default;
  SingletonCell(const SingletonCell&) = delete;
  SingletonCell& operator=(const SingletonCell&) = delete;

  // All concurrent accesses must refer to the same declaration cell, supplied
  // by the future application-domain registry. Cell destruction requires host
  // quiescence; close() is safe concurrently with get().
  template<class Initialize> std::shared_ptr<const T> get(Initialize&& initialize) {
    std::unique_lock lock(mutex_);
    for (;;) {
      if (phase_ == Phase::Closed) throw SingletonClosed();
      if (phase_ == Phase::Ready) return value_;
      if (phase_ == Phase::Failed) std::rethrow_exception(failure_);
      // Conservative bootstrap rule: no initializer may wait for or start
      // another unpublished singleton. This rejects direct and cross-thread
      // synchronous cycles without requiring a global allocating wait graph.
      if (singleton_initializer_depth != 0) throw SingletonInitializationDependency();
      if (phase_ == Phase::Initializing) {
        changed_.wait(lock, [&] { return phase_ != Phase::Initializing; });
        continue;
      }
      phase_ = Phase::Initializing;
      break;
    }
    lock.unlock();
    std::shared_ptr<const T> candidate;
    std::exception_ptr failure;
    ++singleton_initializer_depth;
    try { candidate = std::make_shared<const T>(std::forward<Initialize>(initialize)()); }
    catch (...) { failure = std::current_exception(); }
    --singleton_initializer_depth;
    lock.lock();
    if (phase_ == Phase::Closed) {
      lock.unlock();
      changed_.notify_all();
      throw SingletonClosed();
    }
    if (failure) {
      failure_ = failure;
      phase_ = Phase::Failed;
    } else {
      value_ = candidate;
      phase_ = Phase::Ready;
    }
    lock.unlock();
    changed_.notify_all();
    if (failure) std::rethrow_exception(failure);
    return candidate;
  }

  void close() {
    std::shared_ptr<const T> retired;
    {
      std::lock_guard lock(mutex_);
      phase_ = Phase::Closed;
      retired = std::move(value_);
      // Retain failure until host quiescence: exception payload destruction
      // may execute arbitrary host code and must not run under this lock.
    }
    changed_.notify_all();
    // Existing immutable reader leases retain their value after close.
  }
};
}
