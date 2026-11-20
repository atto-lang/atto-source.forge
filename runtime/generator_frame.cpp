#include "runtime/generator_frame.hpp"

namespace oresforge::runtime {

int GeneratorFrameV1::initialize() noexcept {
  if (state_ != kFresh) return 0;
  state_ = kActive;
  next_slot_ = 0;
  return 1;
}

int GeneratorFrameV1::pull(std::size_t yield_count, std::size_t& slot) noexcept {
  slot = std::numeric_limits<std::size_t>::max();
  if (yield_count > kMaxYields) return -1;
  if (state_ == kTerminal) return 0;
  if (state_ != kActive || next_slot_ > yield_count) return -1;
  if (next_slot_ == yield_count) {
    state_ = kTerminal;
    next_slot_ = 0;
    return 0;
  }
  slot = next_slot_++;
  return 1;
}

int GeneratorFrameV1::close() noexcept {
  if (state_ == kTerminal) return 0;
  if (state_ != kActive) return -1;
  state_ = kTerminal;
  next_slot_ = 0;
  return 1;
}

}  // namespace oresforge::runtime
