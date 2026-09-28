#pragma once

#include "Types.hpp"

#include <cstdint>
#include <atomic>

namespace Horse::Deterministic
{
using UcrtRandFn = int (*)();
using UcrtSrandFn = void (*)(unsigned int);

enum class UcrtRandBrokerMode : std::uint8_t
{
    Disabled,
    Observing,
    Owned,
    Failed,
};

struct UcrtRandBrokerImage
{
    std::uint32_t algorithm_version{};
    std::uint32_t allowlist_version{};
    std::uint32_t state{};
    // state is the actual PTD cursor, including calls bypassing the IAT.
    // draws counts private MoveVM draws only; native_draws counts observed
    // owner-thread IAT calls only. Neither reconstructs native PTD state.
    std::uint64_t draws{};
    bool seeded{};
    std::uint32_t combat_state{};
    std::uint32_t seed_state{};
    std::uint32_t warmup_draws{};
    std::uint64_t native_draws{};
    std::uint64_t unknown_draws{};
    std::uint64_t epoch{};
    bool combat_ready{};

    friend bool operator==(
        const UcrtRandBrokerImage&,
        const UcrtRandBrokerImage&) = default;
};

class UcrtRandBroker
{
public:
    Status Start() noexcept;
    Status BindNative(UcrtRandFn random, UcrtSrandFn seed) noexcept;
    // Explicit completion boundary: caller must be outside native seed/warmup.
    // Production uses ObserveInitializationComplete at the existing xorshift
    // hook, then EnsureOwnership (which cannot complete initialization).
    Status AcquireOwnership(std::uint32_t thread_id) noexcept;
    Status ObserveInitializationComplete(
        std::uint32_t thread_id, std::uintptr_t return_rva) noexcept;
    Status EnsureOwnership(std::uint32_t thread_id) noexcept;
    Status ReleaseOwnership(std::uint32_t thread_id) noexcept;
    void Stop() noexcept;

    int HandleRand(std::uint32_t thread_id, std::uintptr_t return_rva,
        UcrtRandFn original) noexcept;
    void HandleSrand(std::uint32_t thread_id, std::uintptr_t return_rva,
        unsigned int seed, UcrtSrandFn original) noexcept;

    Status Capture(std::uint32_t thread_id, UcrtRandBrokerImage& output) noexcept;
    // Independent boundary observation: no IAT hooks, seeding, stream
    // ownership or draw-count assumptions. Pins the actual thread/PTD.
    Status ObserveNative(std::uint32_t thread_id, std::uint32_t& state) noexcept;
    Status Restore(
        std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept;
    Status PreflightRestore(
        std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept;
    [[nodiscard]] static bool ValidateImage(const UcrtRandBrokerImage& image) noexcept;
    [[nodiscard]] const char* Lane(std::uint32_t thread_id,
        std::uintptr_t return_rva) const noexcept;

    [[nodiscard]] UcrtRandBrokerMode mode() const noexcept { return mode_.load(); }
    [[nodiscard]] FailureCode failure() const noexcept { return failure_.load(); }
    [[nodiscard]] std::uint32_t owner_thread_id() const noexcept
    {
        return owner_thread_id_.load();
    }

private:
    [[nodiscard]] bool IsOwner(std::uint32_t thread_id) const noexcept;
    Status CompleteInitialization(std::uint32_t thread_id) noexcept;
    bool ReadNativeState(std::uint32_t& state) noexcept;
    bool RequireOwner(std::uint32_t thread_id) noexcept;
    void Fail(FailureCode code) noexcept;

    // Foreign native TLS calls can observe admission or report a misplaced
    // combat call. Private cursors remain exclusively owner-thread state.
    std::atomic<UcrtRandBrokerMode> mode_{UcrtRandBrokerMode::Disabled};
    std::atomic<FailureCode> failure_{FailureCode::None};
    std::atomic<std::uint32_t> owner_thread_id_{};
    UcrtRandBrokerImage image_{};
    // Process-local CRT bindings never enter the deterministic image.
    void* (*native_get_ptd_)(){};
    UcrtSrandFn native_seed_{};
    std::uint32_t* native_state_{};
    std::uint32_t native_thread_{};
    std::uint64_t next_epoch_{};
};
}
