#pragma once

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Horse::Deterministic
{
inline constexpr std::uint32_t audio_logical_id_tag = 0x80000000u;
inline constexpr std::uint32_t audio_invalid_playback_id = 0xffffffffu;
inline constexpr std::uint32_t audio_native_id_maximum = 0x7fffu;
inline constexpr std::uint32_t audio_ordinals_per_frame = 64u;
inline constexpr std::size_t maximum_audio_owner_bindings = 136;
inline constexpr std::size_t maximum_audio_playback_mappings = 512;

enum class AudioOwnerDomain : std::uint8_t
{
    BattleClassPlayer,
    BattleSharedPlayer,
    BgmLane,
    Jingle,
    ActiveContextSe,
    ActiveContextVoice,
    ScheduledPlayer,
    BgmDirect,
    BattleCharaPlayer,
    Count,
};

struct AudioOwnerSelector
{
    AudioOwnerDomain domain{AudioOwnerDomain::BattleClassPlayer};
    std::uint8_t index{};
    std::uint16_t scope_id{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        if (domain >= AudioOwnerDomain::Count) return false;
        switch (domain)
        {
        case AudioOwnerDomain::BattleClassPlayer:
        case AudioOwnerDomain::BattleCharaPlayer:
            return index < 64 && scope_id == 0;
        case AudioOwnerDomain::BgmLane:
            return index < 2 && scope_id == 0;
        case AudioOwnerDomain::ScheduledPlayer:
            return index < 11 && scope_id != 0;
        default:
            return index == 0 && scope_id == 0;
        }
    }

    friend constexpr bool operator==(
        const AudioOwnerSelector&, const AudioOwnerSelector&) = default;
};

static_assert(sizeof(AudioOwnerSelector) == 4);

[[nodiscard]] constexpr bool IsLogicalAudioPlaybackId(
    std::uint32_t value) noexcept
{
    return (value & audio_logical_id_tag) != 0
        && value != audio_invalid_playback_id;
}

[[nodiscard]] constexpr bool IsNativeAudioPlaybackId(
    std::uint32_t value) noexcept
{
    return value <= audio_native_id_maximum;
}

[[nodiscard]] constexpr std::uint32_t MakeLogicalAudioPlaybackId(
    std::uint64_t frame, std::uint32_t ordinal) noexcept
{
    if (ordinal >= audio_ordinals_per_frame)
        return audio_invalid_playback_id;
    const auto value = audio_logical_id_tag
        | ((static_cast<std::uint32_t>(frame) & 0x01ffffffu) << 6u)
        | ordinal;
    return value == audio_invalid_playback_id
        ? audio_invalid_playback_id : value;
}

enum class AudioTerminalOperation : std::uint8_t
{
    Create,
    StopOne,
    StopAll,
    SetParameter,
};

struct AudioTerminalEvent
{
    AudioTerminalOperation operation{};
    AudioOwnerSelector owner{};
    std::uint32_t logical_playback_id{audio_invalid_playback_id};
    std::uint32_t cue_sheet_id{};
    std::int32_t cue_id{-1};
    std::uint32_t value{};

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        if (!owner.valid()) return false;
        switch (operation)
        {
        case AudioTerminalOperation::Create:
            return IsLogicalAudioPlaybackId(logical_playback_id)
                && cue_id >= 0;
        case AudioTerminalOperation::StopOne:
            return (IsLogicalAudioPlaybackId(logical_playback_id)
                    || IsNativeAudioPlaybackId(logical_playback_id))
                && cue_sheet_id == 0 && cue_id == -1 && value <= 1;
        case AudioTerminalOperation::StopAll:
            return logical_playback_id == audio_invalid_playback_id
                && cue_sheet_id == 0 && cue_id == -1 && value <= 1;
        case AudioTerminalOperation::SetParameter:
            return logical_playback_id == audio_invalid_playback_id
                && cue_sheet_id < 25 && cue_id == -1;
        }
        return false;
    }

    friend constexpr bool operator==(
        const AudioTerminalEvent&, const AudioTerminalEvent&) = default;
};

static_assert(sizeof(AudioTerminalEvent) == 24);

class AudioOwnerResolver
{
public:
    [[nodiscard]] bool BeginEpoch(std::uint64_t epoch) noexcept
    {
        Clear();
        epoch_ = epoch;
        return epoch != 0;
    }

    void Clear() noexcept
    {
        entries_ = {};
        epoch_ = 0;
        sealed_ = false;
    }

    [[nodiscard]] bool Bind(std::uint64_t epoch, std::uintptr_t owner,
        AudioOwnerSelector selector) noexcept
    {
        if (epoch == 0 || epoch != epoch_ || sealed_ || owner == 0
            || !selector.valid())
            return false;
        Entry* free{};
        for (auto& entry : entries_)
        {
            if (!entry.occupied)
            {
                if (free == nullptr) free = &entry;
                continue;
            }
            if (entry.owner == owner || entry.selector == selector)
                return entry.owner == owner && entry.selector == selector;
        }
        if (free == nullptr) return false;
        *free = {owner, selector, true};
        return true;
    }

    [[nodiscard]] bool Seal(std::uint64_t epoch) noexcept
    {
        if (epoch == 0 || epoch != epoch_) return false;
        sealed_ = true;
        return true;
    }

    [[nodiscard]] bool Resolve(std::uint64_t epoch, std::uintptr_t owner,
        AudioOwnerSelector& output) const noexcept
    {
        output = {};
        if (!sealed_ || epoch == 0 || epoch != epoch_ || owner == 0)
            return false;
        for (const auto& entry : entries_)
            if (entry.occupied && entry.owner == owner)
            {
                output = entry.selector;
                return true;
            }
        return false;
    }

    [[nodiscard]] bool ResolveOwner(std::uint64_t epoch,
        AudioOwnerSelector selector, std::uintptr_t& output) const noexcept
    {
        output = 0;
        if (!sealed_ || epoch == 0 || epoch != epoch_ || !selector.valid())
            return false;
        for (const auto& entry : entries_)
            if (entry.occupied && entry.selector == selector)
            {
                output = entry.owner;
                return true;
            }
        return false;
    }

    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] bool sealed() const noexcept { return sealed_; }
    [[nodiscard]] std::size_t binding_count() const noexcept
    {
        std::size_t count{};
        for (const auto& entry : entries_) count += entry.occupied ? 1u : 0u;
        return count;
    }

    [[nodiscard]] bool SameBindings(
        const AudioOwnerResolver& other) const noexcept
    {
        if (!sealed_ || !other.sealed_) return false;
        for (const auto& entry : entries_)
        {
            if (!entry.occupied) continue;
            AudioOwnerSelector selector{};
            if (!other.Resolve(other.epoch_, entry.owner, selector)
                || selector != entry.selector)
                return false;
        }
        for (const auto& entry : other.entries_)
        {
            if (!entry.occupied) continue;
            AudioOwnerSelector selector{};
            if (!Resolve(epoch_, entry.owner, selector)
                || selector != entry.selector)
                return false;
        }
        return true;
    }

private:
    struct Entry
    {
        std::uintptr_t owner{};
        AudioOwnerSelector selector{};
        bool occupied{};
    };

    std::array<Entry, maximum_audio_owner_bindings> entries_{};
    std::uint64_t epoch_{};
    bool sealed_{};
};

class AudioPlaybackMap
{
public:
    [[nodiscard]] bool BeginEpoch(std::uint64_t epoch) noexcept
    {
        Clear();
        epoch_ = epoch;
        return epoch != 0;
    }

    void Clear() noexcept
    {
        entries_ = {};
        epoch_ = 0;
    }

    [[nodiscard]] bool Insert(std::uint64_t epoch,
        AudioOwnerSelector owner, std::uint32_t logical_id,
        std::uint32_t native_id) noexcept
    {
        if (epoch == 0 || epoch != epoch_ || !owner.valid()
            || !IsLogicalAudioPlaybackId(logical_id)
            || !IsNativeAudioPlaybackId(native_id))
            return false;
        Entry* free{};
        for (auto& entry : entries_)
        {
            if (!entry.occupied)
            {
                if (free == nullptr) free = &entry;
                continue;
            }
            if (entry.owner == owner && entry.logical_id == logical_id)
                return entry.native_id == native_id;
            if (entry.owner == owner && entry.native_id == native_id)
                return false;
        }
        if (free == nullptr) return false;
        *free = {owner, logical_id, native_id, true};
        return true;
    }

    [[nodiscard]] bool CanInsert(std::uint64_t epoch,
        AudioOwnerSelector owner, std::uint32_t logical_id) const noexcept
    {
        if (epoch == 0 || epoch != epoch_ || !owner.valid()
            || !IsLogicalAudioPlaybackId(logical_id))
            return false;
        bool has_free{};
        for (const auto& entry : entries_)
        {
            if (!entry.occupied)
            {
                has_free = true;
                continue;
            }
            if (entry.owner == owner && entry.logical_id == logical_id)
                return true;
        }
        return has_free;
    }

    template <typename IsActive>
    std::size_t PruneInactive(
        std::uint64_t epoch, IsActive&& is_active) noexcept
    {
        if (epoch == 0 || epoch != epoch_) return 0;
        std::size_t removed{};
        for (auto& entry : entries_)
        {
            if (!entry.occupied
                || is_active(entry.owner, entry.native_id))
                continue;
            entry = {};
            ++removed;
        }
        return removed;
    }

    [[nodiscard]] bool LogicalForNative(std::uint64_t epoch,
        AudioOwnerSelector owner, std::uint32_t native_id,
        std::uint32_t& output) const noexcept
    {
        output = audio_invalid_playback_id;
        if (epoch == 0 || epoch != epoch_ || !owner.valid()
            || !IsNativeAudioPlaybackId(native_id))
            return false;
        for (const auto& entry : entries_)
            if (entry.occupied && entry.owner == owner
                && entry.native_id == native_id)
            {
                output = entry.logical_id;
                return true;
            }
        return false;
    }

    [[nodiscard]] bool NativeForLogical(std::uint64_t epoch,
        AudioOwnerSelector owner, std::uint32_t logical_id,
        std::uint32_t& output) const noexcept
    {
        output = audio_invalid_playback_id;
        if (epoch == 0 || epoch != epoch_ || !owner.valid()
            || !IsLogicalAudioPlaybackId(logical_id))
            return false;
        for (const auto& entry : entries_)
            if (entry.occupied && entry.owner == owner
                && entry.logical_id == logical_id)
            {
                output = entry.native_id;
                return true;
            }
        return false;
    }

    void RemoveOwner(std::uint64_t epoch, AudioOwnerSelector owner) noexcept
    {
        if (epoch == 0 || epoch != epoch_ || !owner.valid()) return;
        for (auto& entry : entries_)
            if (entry.occupied && entry.owner == owner) entry = {};
    }

    [[nodiscard]] bool RemoveOne(std::uint64_t epoch,
        AudioOwnerSelector owner, std::uint32_t logical_id) noexcept
    {
        if (epoch == 0 || epoch != epoch_ || !owner.valid()
            || !IsLogicalAudioPlaybackId(logical_id))
            return false;
        for (auto& entry : entries_)
            if (entry.occupied && entry.owner == owner
                && entry.logical_id == logical_id)
            {
                entry = {};
                return true;
            }
        return false;
    }

private:
    struct Entry
    {
        AudioOwnerSelector owner{};
        std::uint32_t logical_id{};
        std::uint32_t native_id{};
        bool occupied{};
    };

    std::array<Entry, maximum_audio_playback_mappings> entries_{};
    std::uint64_t epoch_{};
};
}
