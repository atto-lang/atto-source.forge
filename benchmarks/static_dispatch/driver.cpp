// Stage-0 Forge native AOT microbenchmark, not a trait or virtual-call guest.
// This driver links the exact direct-call assembly emitted from .atto source.
// The function-pointer comparator is a SYNTHETIC HOST dynamic-call control.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>

extern "C" std::int64_t oresforge_entry();
using GuestEntry = std::int64_t (*)();
static GuestEntry volatile runtime_slot = &oresforge_entry;

namespace {
constexpr std::uint64_t kWarmupCalls = 10'000;
constexpr std::uint64_t kCallsPerSample = 800'000;
constexpr std::size_t kSamples = 7;

template<class Fn>
double sample_median_ns(Fn&& fn) {
  std::int64_t checksum=0;
  for(std::uint64_t n=0;n<kWarmupCalls;++n)
    checksum+=fn();
  std::array<double,kSamples> samples{};
  for(std::size_t i=0;i<kSamples;++i) {
    const auto start=std::chrono::steady_clock::now();
    for(std::uint64_t n=0;n<kCallsPerSample;++n)
      checksum+=fn();
    const auto stop=std::chrono::steady_clock::now();
    const auto nanoseconds=
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
    if(nanoseconds<=0) throw std::runtime_error("nonpositive monotonic elapsed time");
    samples[i]=static_cast<double>(nanoseconds)/static_cast<double>(kCallsPerSample);
  }
  const auto expected=static_cast<std::int64_t>(42*(kWarmupCalls+kSamples*kCallsPerSample));
  if(checksum!=expected)
    throw std::runtime_error("benchmark checksum failed; guest was skipped or changed");
  std::sort(samples.begin(),samples.end());
  return samples[kSamples/2];
}
}
int main() {
  try {
    if(oresforge_entry()!=42) throw std::runtime_error("AOT guest did not return 42");
    const auto direct=sample_median_ns([]{return oresforge_entry();});
    const auto synthetic=sample_median_ns([]{
      const GuestEntry f=runtime_slot;
      return f();
    });
    std::cout<<std::fixed<<std::setprecision(3)
      <<"FORGE_STAGE0_DIRECT_AOT_BENCH|direct_ns_per_call="<<direct
      <<"|synthetic_host_indirect_ns_per_call="<<synthetic
      <<"|calls_per_sample="<<kCallsPerSample
      <<"|samples="<<kSamples
      <<"|result=42"
      <<"|synthetic_is_not_atto_closed_indirect=true\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"FORGE_STAGE0_BENCH_REJECT: "<<error.what()<<'\n';
    return 1;
  }
}
