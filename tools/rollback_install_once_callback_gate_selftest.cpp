#include "RollbackInstallOnceCallbackGate.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

namespace
{
    std::atomic<uint64_t> g_original_calls {0};

    void OriginalTrampoline() noexcept
    {
        g_original_calls.fetch_add(1, std::memory_order_relaxed);
    }
}

int main()
{
    Horse::RollbackInstallOnceCallbackGate gate {};
    std::atomic<bool> owns {true};
    std::atomic<bool> stop {false};
    std::atomic<bool> start {false};
    std::atomic<uint64_t> callbacks {0};
    std::atomic<uint64_t> owned {0};
    std::atomic<uint64_t> pass_through {0};
    using Trampoline = void(*)() noexcept;
    const Trampoline trampoline = &OriginalTrampoline;

    auto dispatch_once = [&]() noexcept {
        bool admitted = false;
        gate.enter(admitted);
        callbacks.fetch_add(1, std::memory_order_relaxed);
        if (admitted && owns.load(std::memory_order_acquire))
            owned.fetch_add(1, std::memory_order_relaxed);
        else
        {
            trampoline();
            pass_through.fetch_add(1, std::memory_order_relaxed);
        }
        gate.leave(admitted);
    };

    // Deterministically prove that close waits for a callback admitted before
    // closure, while callbacks entering after closure remain pass-through.
    std::mutex held_mutex;
    std::condition_variable held_cv;
    bool held_entered = false;
    bool release_held = false;
    bool held_was_admitted = false;
    gate.open();
    bool depth_probe_admitted = false;
    gate.enter(depth_probe_admitted);
    const bool depth_guard_entered = depth_probe_admitted
        && Horse::RollbackInstallOnceCallbackGate::
            current_thread_inside_callback();
    gate.leave(depth_probe_admitted);
    if (!depth_guard_entered
        || Horse::RollbackInstallOnceCallbackGate::
            current_thread_inside_callback())
        return 1;
    std::thread held([&]() noexcept {
        gate.enter(held_was_admitted);
        {
            std::lock_guard<std::mutex> lock(held_mutex);
            held_entered = true;
        }
        held_cv.notify_all();
        std::unique_lock<std::mutex> lock(held_mutex);
        held_cv.wait(lock, [&]() { return release_held; });
        lock.unlock();
        gate.leave(held_was_admitted);
    });
    {
        std::unique_lock<std::mutex> lock(held_mutex);
        held_cv.wait(lock, [&]() { return held_entered; });
    }
    if (!held_was_admitted)
    {
        {
            std::lock_guard<std::mutex> lock(held_mutex);
            release_held = true;
        }
        held_cv.notify_all();
        held.join();
        return 1;
    }

    std::atomic<bool> close_started {false};
    std::atomic<bool> close_returned {false};
    std::thread closer([&]() noexcept {
        close_started.store(true, std::memory_order_release);
        gate.close_and_drain();
        close_returned.store(true, std::memory_order_release);
    });
    while (!close_started.load(std::memory_order_acquire)
           || gate.accepting())
        std::this_thread::yield();
    dispatch_once();
    const bool closed_callback_ok = pass_through.load() == 1
        && g_original_calls.load() == 1
        && !close_returned.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(held_mutex);
        release_held = true;
    }
    held_cv.notify_all();
    held.join();
    closer.join();
    if (!closed_callback_ok || !close_returned.load()
        || gate.inflight() != 0)
        return 2;

    bool post_close_entered = false;
    bool release_post_close = false;
    bool post_close_admitted = true;
    std::thread post_close([&]() noexcept {
        gate.enter(post_close_admitted);
        callbacks.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(held_mutex);
            post_close_entered = true;
        }
        held_cv.notify_all();
        std::unique_lock<std::mutex> lock(held_mutex);
        held_cv.wait(lock, [&]() { return release_post_close; });
        lock.unlock();
        trampoline();
        pass_through.fetch_add(1, std::memory_order_relaxed);
        gate.leave(post_close_admitted);
    });
    {
        std::unique_lock<std::mutex> lock(held_mutex);
        held_cv.wait(lock, [&]() { return post_close_entered; });
    }
    // Logical cleanup is allowed to proceed while this closed-gate callback
    // is inside the detour because the process-lifetime trampoline is kept.
    const Trampoline retained_trampoline = trampoline;
    const bool post_close_safe = !post_close_admitted
        && gate.inflight() == 0
        && retained_trampoline == &OriginalTrampoline;
    {
        std::lock_guard<std::mutex> lock(held_mutex);
        release_post_close = true;
    }
    held_cv.notify_all();
    post_close.join();
    if (!post_close_safe || pass_through.load() != 2
        || g_original_calls.load() != 2)
        return 3;

    gate.open();
    dispatch_once();
    if (owned.load() != 1 || g_original_calls.load() != 2) return 4;

    auto worker = [&]() noexcept {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        while (!stop.load(std::memory_order_acquire))
        {
            dispatch_once();
            std::this_thread::yield();
        }
    };

    std::thread first(worker);
    std::thread second(worker);
    start.store(true, std::memory_order_release);
    while (owned.load(std::memory_order_acquire) < 10)
        std::this_thread::yield();
    bool stress_ok = true;
    for (uint32_t cycle = 0; cycle < 100; ++cycle)
    {
        owns.store(false, std::memory_order_release);
        gate.close_and_drain();
        if (gate.accepting())
        {
            stress_ok = false;
            break;
        }
        const uint64_t pass_before = pass_through.load();
        while (pass_through.load(std::memory_order_acquire) == pass_before)
            std::this_thread::yield();
        gate.open();
        owns.store(true, std::memory_order_release);
        const uint64_t owned_before = owned.load();
        while (owned.load(std::memory_order_acquire) == owned_before)
            std::this_thread::yield();
    }

    stop.store(true, std::memory_order_release);
    owns.store(false, std::memory_order_release);
    gate.close_and_drain();
    first.join();
    second.join();

    const uint64_t callback_count = callbacks.load();
    const uint64_t owned_count = owned.load();
    const uint64_t pass_count = pass_through.load();
    const bool ok = callback_count != 0
        && stress_ok
        && owned_count != 0
        && pass_count != 0
        && callback_count == owned_count + pass_count
        && g_original_calls.load() == pass_count
        && trampoline == &OriginalTrampoline
        && gate.inflight() == 0
        && !gate.accepting();
    if (!ok) return 5;
    std::puts("rollback install-once callback gate self-test passed");
    return 0;
}
