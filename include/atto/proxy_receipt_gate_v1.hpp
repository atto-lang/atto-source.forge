#pragma once
// Host-owned receipt-state gate (Atto proxy ACK v1). NOT a wire protocol,
// authentication verifier, IPC implementation, or compiled Atto guest API.
// Only a trusted transport/runtime may call apply_verified() AFTER checking
// cryptographic peer identity, authorization, contract, and destination proof.
#include <cstdint>

namespace atto::ipc {
struct receipt_identity {
    std::uint32_t protocol_major;
    std::uint64_t tenant;
    std::uint64_t caller_incarnation;
    std::uint64_t target_actor;
    std::uint64_t target_incarnation;
    std::uint64_t association_generation;
    std::uint64_t placement_epoch;
    std::uint64_t message_id;
    std::uint64_t attempt;
    bool operator==(const receipt_identity&) const = default;
};

enum class receipt_event {
    admitted, started, processed, durable_inbox, durable_result,
    rejected, failed, indeterminate
};
enum class receipt_stage { dispatched, admitted, started, processed };
enum class receipt_outcome { pending, rejected, failed, indeterminate };

class proxy_receipt_gate final {
    receipt_identity expected_;
    receipt_stage stage_ = receipt_stage::dispatched;
    receipt_outcome outcome_ = receipt_outcome::pending;
    bool durable_inbox_ = false;
    bool durable_result_ = false;

public:
    explicit proxy_receipt_gate(receipt_identity expected) : expected_(expected) {}
    receipt_stage stage() const noexcept { return stage_; }
    receipt_outcome outcome() const noexcept { return outcome_; }
    bool durable_inbox() const noexcept { return durable_inbox_; }
    bool durable_result() const noexcept { return durable_result_; }
    bool destination_admitted() const noexcept {
        return stage_ >= receipt_stage::admitted;
    }
    bool apply_verified(const receipt_identity& incoming, receipt_event event) noexcept {
        if (!(incoming == expected_) || expected_.protocol_major != 1 ||
            expected_.tenant == 0 || expected_.caller_incarnation == 0 ||
            expected_.target_actor == 0 || expected_.target_incarnation == 0 || expected_.association_generation == 0 ||
            expected_.placement_epoch == 0 || expected_.message_id == 0 ||
            expected_.attempt == 0 || outcome_ != receipt_outcome::pending)
            return false;
        switch (event) {
        case receipt_event::admitted:
            if (stage_ < receipt_stage::admitted) stage_ = receipt_stage::admitted;
            return true; // same delivery, authenticated repeat; no regression
        case receipt_event::started:
            if (stage_ < receipt_stage::admitted) return false;
            if (stage_ < receipt_stage::started) stage_ = receipt_stage::started;
            return true;
        case receipt_event::processed:
            if (stage_ < receipt_stage::started) return false;
            stage_ = receipt_stage::processed;
            return true; // successful turn commit, not merely callback entry
        case receipt_event::durable_inbox:
            if (stage_ < receipt_stage::admitted) return false;
            durable_inbox_ = true;
            return true; // never implies processing success
        case receipt_event::durable_result:
            if (stage_ != receipt_stage::processed) return false;
            durable_result_ = true;
            return true; // requires separate provider-confirmed commit
        case receipt_event::rejected:
            if (stage_ != receipt_stage::dispatched) return false;
            outcome_ = receipt_outcome::rejected;
            return true;
        case receipt_event::failed:
            if (stage_ < receipt_stage::admitted || stage_ == receipt_stage::processed)
                return false;
            outcome_ = receipt_outcome::failed;
            return true;
        case receipt_event::indeterminate:
            if (stage_ == receipt_stage::processed) return false;
            outcome_ = receipt_outcome::indeterminate;
            return true; // disconnect/timeout does NOT mean rejection
        }
        return false;
    }
};
} // namespace atto::ipc
