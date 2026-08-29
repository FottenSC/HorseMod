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
    void ReleasePlaybackContext() noexcept;

private:
    void* playback_container_{};
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
    }
    return "unknown";
}
}
