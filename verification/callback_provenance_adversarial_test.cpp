#include "runtime/execution_callback_context.hpp"

#include <cstdlib>
#include <iostream>

using namespace ores::runtime;

int main() {
    int failures = 0;
    int cases = 0;
    auto check = [&](bool ok, const char* name) {
        ++cases;
        if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; }
    };
    const ExecutionContextValue active{17, 41, 7, 3, 11, 5};
    ExecutionCallbackContext good{};
    check(capture_execution_callback_context(active, CallbackBoundary::io, good), "capture");
    ExecutionCallbackContext result{};
    check(derive_execution_callback_context(good, CallbackBoundary::timer, result)
        && valid_execution_callback_context(result), "valid derivation");
    auto reject = [&](ExecutionCallbackContext bad, const char* name) {
        auto sentinel = good;
        bool ok = !derive_execution_callback_context(bad, CallbackBoundary::timer, sentinel)
            && sentinel.execution == good.execution && sentinel.boundary == good.boundary
            && sentinel.trace_count == good.trace_count;
        check(ok, name);
    };
    auto bad = good; bad.runtime_domain = 0; reject(bad, "zero runtime domain");
    bad = good; bad.execution = 0; reject(bad, "zero execution");
    bad = good; bad.dispatch = 0; reject(bad, "zero dispatch");
    bad = good; bad.generation = 0; reject(bad, "zero generation");
    bad = good; bad.boundary = static_cast<CallbackBoundary>(99); reject(bad, "invalid parent boundary");
    bad = good; bad.trace_boundaries[0] = CallbackBoundary::timer; reject(bad, "mismatched trace tail");
    bad = result; bad.trace_boundaries[0] = static_cast<CallbackBoundary>(99); reject(bad, "invalid historical boundary");
    bad = good; bad.trace_elided = 1; reject(bad, "impossible elision on derive");
    check(!valid_execution_callback_context(bad), "impossible elision on validate");
    check(!derive_execution_callback_context(good, static_cast<CallbackBoundary>(99), result), "invalid next boundary");
    result = good;
    bool derived = true;
    for (int i = 0; i < 12; ++i) derived = derive_execution_callback_context(result, CallbackBoundary::timer, result) && derived;
    check(derived && result.trace_count == 8 && result.trace_elided == 5 && valid_execution_callback_context(result), "in-place bounded derivation");
    result.trace_elided = UINT64_MAX;
    check(derive_execution_callback_context(result, CallbackBoundary::io, result) && result.trace_elided == UINT64_MAX, "elision saturates");
    std::cout << cases - failures << '/' << cases << " passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
