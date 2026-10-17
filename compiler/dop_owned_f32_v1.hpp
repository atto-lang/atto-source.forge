#pragma once

// Host-only owned F32 region foundation; no guest cpu/gpu/nb lowering,
// no device/stream scheduling, and no implicit GPU fallback.
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace oresforge::dop_v1 {

enum class Layout { AoS, SoA, AoSoA };
enum class Mode { Read, Write };

struct Shape {
    Layout layout;
    std::uint64_t rows;
    std::uint32_t fields;
    std::uint32_t lanes = 1;

    std::size_t physical_cells() const {
        if (rows > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            fields == 0 || fields > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
            lanes == 0 || lanes > 1024 ||
            (layout != Layout::AoSoA && lanes != 1) ||
            (layout == Layout::AoSoA && (lanes & (lanes - 1)) != 0) ||
            (layout != Layout::AoS && layout != Layout::SoA && layout != Layout::AoSoA))
            throw std::invalid_argument("invalid owned F32 region layout");
        const auto padded = layout == Layout::AoSoA
            ? (rows / lanes + (rows % lanes != 0 ? 1 : 0)) * lanes : rows;
        if (padded > std::numeric_limits<std::size_t>::max() / fields ||
            padded * fields > std::numeric_limits<std::size_t>::max() / sizeof(float))
            throw std::length_error("owned region byte offset overflow");
        const auto bytes = static_cast<std::size_t>(padded * fields) * sizeof(float);
        if (bytes > 128u * 1024u * 1024u)
            throw std::length_error("owned F32 region exceeds host memory budget");
        return bytes / sizeof(float);
    }
    std::size_t byte_offset(std::uint64_t row, std::uint32_t field) const {
        if (row >= rows || field >= fields) throw std::out_of_range("F32 region coordinate");
        std::uint64_t cell = 0;
        switch (layout) {
            case Layout::AoS: cell = row * fields + field; break;
            case Layout::SoA: cell = static_cast<std::uint64_t>(field) * rows + row; break;
            case Layout::AoSoA:
                cell = (row / lanes) * static_cast<std::uint64_t>(fields) * lanes +
                    static_cast<std::uint64_t>(field) * lanes + row % lanes;
                break;
            default: throw std::invalid_argument("invalid layout kind");
        }
        return static_cast<std::size_t>(cell) * sizeof(float);
    }
    bool operator==(const Shape&) const = default;
};

class HostBuffer {
    struct State {
        const Shape shape;
        std::vector<unsigned char> bytes;
        std::mutex gate;
        std::size_t readers = 0;
        bool writer = false;
        bool closed = false;
        explicit State(Shape value)
            : shape(value), bytes(value.physical_cells() * sizeof(float), 0) {}
    };
    std::shared_ptr<State> state_;
public:
    class Area {
        friend class HostBuffer;
        friend void execute_cpu_region(const HostBuffer&, HostBuffer&, const Area&, const Area&, float, float);
        std::weak_ptr<State> owner_;
        Area(const std::shared_ptr<State>& state, Mode mode, std::uint32_t field,
             std::uint64_t begin, std::uint64_t end)
            : owner_(state), mode_(mode), field_(field), begin_(begin), end_(end) {}
        Mode mode_;
        std::uint32_t field_;
        std::uint64_t begin_, end_;
    public:
        Area(const Area&) = default;
        Area& operator=(const Area&) = default;
    };
    class Loan {
        friend class HostBuffer;
        std::shared_ptr<State> state_;
        bool write_;
        explicit Loan(std::shared_ptr<State> state, bool write)
            : state_(std::move(state)), write_(write) {}
    public:
        Loan(const Loan&) = delete;
        Loan& operator=(const Loan&) = delete;
        Loan(Loan&& other) noexcept : state_(std::move(other.state_)), write_(other.write_) {}
        Loan& operator=(Loan&&) = delete;
        ~Loan() noexcept { release(); }
        void release() noexcept {
            auto state = std::move(state_);
            if (!state) return;
            std::lock_guard<std::mutex> guard(state->gate);
            if (write_) state->writer = false;
            else --state->readers;
        }
        float read(std::uint64_t row, std::uint32_t field) const {
            if (!state_) throw std::logic_error("retired F32 loan");
            std::lock_guard<std::mutex> guard(state_->gate);
            if (state_->closed) throw std::logic_error("closed F32 loan");
            float value = 0;
            std::memcpy(&value, state_->bytes.data() + state_->shape.byte_offset(row, field), sizeof(value));
            return value;
        }
        void write(std::uint64_t row, std::uint32_t field, float value) {
            if (!state_ || !write_) throw std::logic_error("not a live F32 write loan");
            std::lock_guard<std::mutex> guard(state_->gate);
            if (state_->closed) throw std::logic_error("closed F32 write loan");
            std::memcpy(state_->bytes.data() + state_->shape.byte_offset(row, field), &value, sizeof(value));
        }
    };
    explicit HostBuffer(Shape shape) : state_(std::make_shared<State>(shape)) {
        static_assert(CHAR_BIT == 8 && sizeof(float) == 4);
        static_assert(std::numeric_limits<float>::is_iec559);
        const std::uint32_t probe = 1;
        if (*reinterpret_cast<const unsigned char*>(&probe) != 1)
            throw std::runtime_error("big-endian owned F32 unsupported");
    }
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    const Shape& shape() const noexcept { return state_->shape; }
    Area area(Mode mode, std::uint32_t field, std::uint64_t begin, std::uint64_t end) const {
        std::lock_guard<std::mutex> guard(state_->gate);
        if (state_->closed || (mode != Mode::Read && mode != Mode::Write) ||
            field >= shape().fields || begin > end || end > shape().rows)
            throw std::invalid_argument("invalid/closed F32 region footprint");
        return Area(state_, mode, field, begin, end);
    }
    Loan borrow_read() const {
        std::lock_guard<std::mutex> guard(state_->gate);
        if (state_->closed || state_->writer || state_->readers == std::numeric_limits<std::size_t>::max())
            throw std::logic_error("F32 buffer unavailable for read");
        ++state_->readers;
        return Loan(state_, false);
    }
    Loan borrow_write() {
        std::lock_guard<std::mutex> guard(state_->gate);
        if (state_->closed || state_->writer || state_->readers)
            throw std::logic_error("F32 buffer unavailable for write");
        state_->writer = true;
        return Loan(state_, true);
    }
    float read(std::uint64_t row, std::uint32_t field) const {
        auto loan = borrow_read(); return loan.read(row, field);
    }
    void write(std::uint64_t row, std::uint32_t field, float value) {
        auto loan = borrow_write(); loan.write(row, field, value);
    }
    void close() {
        std::lock_guard<std::mutex> guard(state_->gate);
        if (state_->readers || state_->writer)
            throw std::logic_error("cannot close borrowed F32 storage");
        state_->closed = true;
        std::vector<unsigned char> released;
        state_->bytes.swap(released);
    }
    friend void execute_cpu_region(const HostBuffer&, HostBuffer&, const Area&, const Area&, float, float);
};

// This is a synchronous trusted-host primitive, not an actor-carrier API.
// No output bytes are modified until BOTH physical loans have been acquired.
inline void execute_cpu_region(const HostBuffer& input, HostBuffer& output,
        const HostBuffer::Area& source, const HostBuffer::Area& destination,
        float scale, float bias) {
    if (&input == &output || input.state_ == output.state_ ||
        !(input.shape() == output.shape()) || !std::isfinite(scale) || !std::isfinite(bias) ||
        source.mode_ != Mode::Read || destination.mode_ != Mode::Write ||
        source.field_ != destination.field_ || source.begin_ != destination.begin_ ||
        source.end_ != destination.end_ ||
        source.owner_.lock() != input.state_ || destination.owner_.lock() != output.state_)
        throw std::invalid_argument("invalid owned F32 CPU region admission");
    auto reader = input.borrow_read();
    auto writer = output.borrow_write();
    for (auto row = source.begin_; row < source.end_; ++row) {
        const auto value = reader.read(row, source.field_);
        writer.write(row, destination.field_, value * scale + bias);
    }
}

} // namespace oresforge::dop_v1
