#pragma once

#include "OnlineCoordinator.hpp"

#ifndef GEKKONET_STATIC
#define GEKKONET_STATIC
#endif
#include <gekkonet.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Horse::Deterministic
{
struct GekkoAdvanceValue
{
    std::int32_t frame{};
    std::array<PlayerInput, 2> inputs{};
    bool rolling_back{};
    bool running_ahead{};
};

class IGekkoSimulationSink
{
public:
    virtual ~IGekkoSimulationSink() = default;
    virtual Status Save(std::int32_t frame, GekkoSaveKind kind,
        std::span<std::byte> destination, std::uint32_t& written,
        std::uint32_t& checksum) noexcept = 0;
    virtual Status Load(
        std::int32_t frame, std::span<const std::byte> state) noexcept = 0;
    virtual Status Advance(const GekkoAdvanceValue& value) noexcept = 0;
};

class GekkoRollbackSession final
{
public:
    static constexpr std::size_t maximum_state_bytes = 256;

    GekkoRollbackSession() noexcept = default;
    ~GekkoRollbackSession();
    GekkoRollbackSession(const GekkoRollbackSession&) = delete;
    GekkoRollbackSession& operator=(const GekkoRollbackSession&) = delete;

    Status Start(OnlineCoordinator& coordinator, IGekkoSimulationSink& sink,
        std::uint8_t local_player_slot, std::uint8_t input_delay,
        std::uint8_t rollback_window, std::uint32_t state_bytes) noexcept;
    Status PollNetwork() noexcept;
    Status FlushCorrections() noexcept;
    [[nodiscard]] bool ReadyForBaseline() const noexcept;
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool IsClearForStock() const noexcept { return !started_; }
    Status Advance(const PlayerInput& local_input,
        std::array<PlayerInput, 2>& authoritative) noexcept;
    Status CompleteDeferredSaves() noexcept;
    [[nodiscard]] std::int32_t confirmed_frame() const noexcept;
    [[nodiscard]] FailureCode terminal_failure() const noexcept;
    void Stop() noexcept;

private:
    static void SendData(
        GekkoNetAddress* address, const char* data, int length) noexcept;
    static GekkoNetResult** ReceiveData(int* length) noexcept;
    static void FreeData(void* data) noexcept;

    Status process_session_events() noexcept;
    Status process_game_events(GekkoGameEvent** events, int count,
        std::array<PlayerInput, 2>* authoritative) noexcept;
    Status complete_save(GekkoGameEvent& event) noexcept;
    Status fail(FailureCode code) noexcept;

    static thread_local GekkoRollbackSession* current_adapter_owner_;
    static constexpr std::size_t maximum_receive_batch = 64;

    OnlineCoordinator* coordinator_{};
    IGekkoSimulationSink* sink_{};
    GekkoSession* session_{};
    GekkoNetAdapter adapter_{};
    std::uint64_t peer_address_token_{1};
    std::array<OnlineGekkoPacket, maximum_receive_batch> receive_packets_{};
    std::array<GekkoNetResult, maximum_receive_batch> receive_results_{};
    std::array<GekkoNetResult*, maximum_receive_batch> receive_result_ptrs_{};
    std::uint32_t state_bytes_{};
    std::uint8_t local_player_slot_{};
    std::uint8_t input_delay_{};
    FailureCode failure_{FailureCode::None};
    bool started_{};
    bool session_started_{};
    std::array<GekkoGameEvent*, 4> deferred_saves_{};
    std::size_t deferred_save_count_{};
};
}
