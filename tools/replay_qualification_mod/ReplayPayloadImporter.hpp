#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Horse::Qualification
{
enum class ImportFailure : std::uint8_t
{
    None,
    UnsupportedExecutable,
    InvalidPayload,
    InitializeFailed,
    DecompressFailed,
    DeserializeFailed,
    SaveManagerUnavailable,
    CopyFailed,
    ContainerCreateFailed,
    PlaybackContextCopyFailed,
    DestroyFailed,
    InvalidMetadata,
    IdentityMismatch,
};

struct ReplayMetadata
{
    static constexpr std::size_t kMaximumStateResetRecords = 16;
    static constexpr std::size_t kMaximumRoundStarts =
        kMaximumStateResetRecords;
    static constexpr std::int8_t kSimultaneousRoundWinners = 2;

    std::int32_t stage_index{-1};
    std::uint8_t left_character{0xff};
    std::uint8_t right_character{0xff};
    std::uint32_t state_reset_record_count{};
    std::int32_t recorded_match_winner{-1}; // Offline oracle; never simulation input.
};

class ReplayPayloadImporter final
{
public:
    ~ReplayPayloadImporter();

    bool Bind(std::uintptr_t image_base) noexcept;
    ImportFailure Import(std::span<const std::byte> payload,
                         ReplayMetadata& metadata) noexcept;
    bool RequestPlayerProfiles() noexcept;
    bool PopulateFallbackProfiles() noexcept;
    bool RequestReadyPlayback() noexcept;
    bool VerifyPlaybackRecording(void* replay_player, std::array<std::uint8_t, 32>& actual) noexcept;
    const std::array<std::uint8_t, 32>& battle_identity() const noexcept { return battle_identity_; }
    const std::array<std::uint8_t, 32>& recording_identity() const noexcept { return recording_identity_; }
    std::string_view identity_phase() const noexcept { return identity_phase_; }
    void ReleasePlaybackContext() noexcept;

private:
    void* playback_container_{};
    std::array<std::uint8_t, 32> battle_identity_{};
    std::array<std::uint8_t, 32> recording_identity_{};
    std::uint32_t replay_version_{};
    bool identity_valid_{};
    std::string_view identity_phase_{"none"};
};

constexpr std::string_view import_failure_name(ImportFailure failure) noexcept
{
    switch (failure)
    {
    case ImportFailure::None: return "none";
    case ImportFailure::UnsupportedExecutable: return "unsupported_executable";
    case ImportFailure::InvalidPayload: return "invalid_payload";
    case ImportFailure::InitializeFailed: return "initialize_failed";
    case ImportFailure::DecompressFailed: return "decompress_failed";
    case ImportFailure::DeserializeFailed: return "deserialize_failed";
    case ImportFailure::SaveManagerUnavailable: return "save_manager_unavailable";
    case ImportFailure::CopyFailed: return "copy_failed";
    case ImportFailure::ContainerCreateFailed: return "container_create_failed";
    case ImportFailure::PlaybackContextCopyFailed:
        return "playback_context_copy_failed";
    case ImportFailure::DestroyFailed: return "destroy_failed";
    case ImportFailure::InvalidMetadata: return "invalid_metadata";
    case ImportFailure::IdentityMismatch: return "payload_identity_mismatch";
    }
    return "unknown";
}
}
