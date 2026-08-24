#pragma once

#include "Sc6ReplayNativeBridge.hpp"

#include <optional>

namespace Horse
{
class Lux;

namespace Deterministic
{
class Sc6ReplayRuntime final
{
public:
    explicit Sc6ReplayRuntime(Lux& lux) noexcept;

    Status Initialize(std::uintptr_t image_base) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] IReplayNativeBridge* bridge() noexcept;

private:
    static void* ResolveReplayPlayer(void* user) noexcept;
    static void* ResolveBattleManager(void* user) noexcept;
    static void* ResolveFighterOne(void* user) noexcept;
    static void* ResolveFighterTwo(void* user) noexcept;
    static void* ResolveStage(void* user) noexcept;

    [[nodiscard]] void* ResolveFighter(std::size_t index) noexcept;

    Lux& lux_;
    std::optional<Sc6ReplayNativeBridge> bridge_{};
};
}
}
