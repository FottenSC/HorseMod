#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace Horse::Deterministic {
// The shipped PxBaseTask completion ABI, used for a privately owned native
// retirement operation. The enclosing operation retains this object through
// native completion; a timeout never releases that ownership. This is not a
// simulation task and does not publish a historical engine task manager.
class ReplayPhysicsTaskCompletion final {
public:
    // Native setContinuation copies these two fields from its continuation.
    std::uint64_t context{};
    void* manager{};

    virtual ~ReplayPhysicsTaskCompletion() = default;
    virtual void run() {}
    virtual const char* name() const { return "replay-physics-retirement"; }
    virtual void add() {
        auto count = references_.load(std::memory_order_acquire);
        while (count > 0 && count < (std::numeric_limits<int>::max)()) {
            if (references_.compare_exchange_weak(count, count + 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) return;
        }
        invalid_.store(true, std::memory_order_release);
    }
    virtual void remove() {
        auto count = references_.load(std::memory_order_acquire);
        while (count > 0) {
            if (references_.compare_exchange_weak(count, count - 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                if (count == 1) { run(); release(); }
                return;
            }
        }
        invalid_.store(true, std::memory_order_release);
    }
    virtual int count() const { return references_.load(std::memory_order_acquire); }
    virtual void release() {
        if (references_.load(std::memory_order_acquire) != 0) {
            invalid_.store(true, std::memory_order_release);
            return; // Native references still own pending work.
        }
        if (releases_.fetch_add(1, std::memory_order_acq_rel) != 0)
            invalid_.store(true, std::memory_order_release);
        completed_.store(true, std::memory_order_release);
    }

    bool completed() const noexcept { return completed_.load(std::memory_order_acquire); }
    bool valid() const noexcept { return !invalid_.load(std::memory_order_acquire); }
    bool completed_once() const noexcept {
        return completed() && valid() && count() == 0
            && releases_.load(std::memory_order_acquire) == 1;
    }
    // An invalid receipt remains invalid. No reset/reopen can erase a native
    // reference bookkeeping failure or make an in-flight object reusable.
private:
    std::atomic<int> references_{1};
    std::atomic<unsigned> releases_{};
    std::atomic<bool> completed_{}, invalid_{};
};
}
