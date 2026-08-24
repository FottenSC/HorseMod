#pragma once

#include "NativeReplayMaterializer.hpp"

namespace Horse::Deterministic
{
using ResolveReplayObjectFn = void* (*)(void* user) noexcept;
using SetBattleManagerMoveStateFn = void (*)(void* battle_manager, std::uint8_t state) noexcept;

struct Sc6ReplayResolvers
{
    void* user{};
    ResolveReplayObjectFn replay_player{};
    ResolveReplayObjectFn battle_manager{};
    ResolveReplayObjectFn fighter_one{};
    ResolveReplayObjectFn fighter_two{};
    ResolveReplayObjectFn stage{};
    SetBattleManagerMoveStateFn set_move_state{};
    bool set_move_state_signature_valid{};
};

class Sc6ReplayNativeBridge final : public IReplayNativeBridge
{
public:
    explicit Sc6ReplayNativeBridge(Sc6ReplayResolvers resolvers) noexcept;

    Status InspectRound(
        std::uint32_t native_round_index,
        ReplayNativeRoundView& output) noexcept override;
    Status RequestRoundReset(
        std::uint32_t native_round_index,
        std::uint64_t round_image_identity) noexcept override;

    [[nodiscard]] static bool ValidateMoveStateSetter(
        std::uintptr_t image_base) noexcept;

private:
    struct ResolvedObjects
    {
        std::byte* replay_player{};
        std::byte* battle_manager{};
        void* fighter_one{};
        void* fighter_two{};
        void* stage{};
        std::byte* round_images{};
        std::int32_t round_count{};
        std::int32_t round_capacity{};
    };

    [[nodiscard]] Status resolve(ResolvedObjects& output) const noexcept;
    [[nodiscard]] Status inspect_resolved(
        const ResolvedObjects& objects,
        std::uint32_t native_round_index,
        ReplayNativeRoundView& output) const noexcept;
    [[nodiscard]] Status undo(
        const ResolvedObjects& objects,
        const std::array<std::byte, Schema::replay_round_image_size>& image,
        std::uint8_t move_state) const noexcept;

    Sc6ReplayResolvers resolvers_{};
};
}
