#pragma once

#include "Types.hpp"

#include <cstdint>

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
    std::uint64_t draws{};
    bool seeded{};

    friend bool operator==(
        const UcrtRandBrokerImage&,
        const UcrtRandBrokerImage&) = default;
};

class UcrtRandBroker
{
public:
    Status Start(std::uint32_t owner_thread_id) noexcept;
    Status AcquireOwnership(std::uint32_t thread_id) noexcept;
    void Stop() noexcept;

    int HandleRand(std::uint32_t thread_id, std::uintptr_t return_rva,
        UcrtRandFn original) noexcept;
    void HandleSrand(std::uint32_t thread_id, std::uintptr_t return_rva,
        unsigned int seed, UcrtSrandFn original) noexcept;

    Status Capture(std::uint32_t thread_id, UcrtRandBrokerImage& output) noexcept;
    Status Restore(
        std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept;

    [[nodiscard]] UcrtRandBrokerMode mode() const noexcept { return mode_; }
    [[nodiscard]] FailureCode failure() const noexcept { return failure_; }

private:
    [[nodiscard]] static bool IsRandCallsite(std::uintptr_t return_rva) noexcept;
    [[nodiscard]] static std::uint32_t Advance(std::uint32_t& state) noexcept;
    bool RequireOwner(std::uint32_t thread_id) noexcept;
    void Fail(FailureCode code) noexcept;

    UcrtRandBrokerMode mode_{UcrtRandBrokerMode::Disabled};
    FailureCode failure_{FailureCode::None};
    std::uint32_t owner_thread_id_{};
    UcrtRandBrokerImage image_{};
};
}
