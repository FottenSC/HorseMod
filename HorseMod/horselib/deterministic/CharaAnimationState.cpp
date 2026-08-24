#include "CharaAnimationState.hpp"

#include <algorithm>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
constexpr std::size_t packed_section_table_offset_index = 3;
constexpr std::size_t clip_binding_offset = 8;
constexpr std::size_t clip_scalar_offset = 0x10;
constexpr std::size_t clip_active_scalar_offset = 0x18;
constexpr std::size_t clip_bootstrap_scalar_offset = 0x1C;
constexpr std::size_t cue_owner_scalar_offset = 8;
constexpr std::size_t cue_owner_enst_offset = 0x28;
constexpr std::size_t cue_owner_scheduler_offset = 0x30;
constexpr std::size_t scheduler_scalar_offset = 0x10;
constexpr std::size_t scheduler_list_head_offset = 0x70;
constexpr std::size_t scheduler_list_count_offset = 0x78;
constexpr std::size_t trigger_scalar_offset = 8;

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address,
    T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
bool write_value(INativeMemory& memory, std::uintptr_t address,
    const T& value) noexcept
{
    return memory.Write(address, std::as_bytes(std::span{&value, 1}));
}

void append(std::vector<std::byte>& output, const void* data, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(data);
    output.insert(output.end(), first, first + size);
}

bool clip_is_active(const CharaAnimationPlayerImage& player) noexcept
{
    std::uint32_t active{};
    static_assert(clip_active_scalar_offset + sizeof(active)
        <= std::tuple_size_v<decltype(player.clip_scalars)>);
    std::memcpy(&active,
        player.clip_scalars.data() + clip_active_scalar_offset,
        sizeof(active));
    return active != 0;
}

bool clip_is_bootstrapped(const CharaAnimationPlayerImage& player) noexcept
{
    std::uint32_t bootstrap{};
    static_assert(clip_bootstrap_scalar_offset + sizeof(bootstrap)
        <= std::tuple_size_v<decltype(player.clip_scalars)>);
    std::memcpy(&bootstrap,
        player.clip_scalars.data() + clip_bootstrap_scalar_offset,
        sizeof(bootstrap));
    return bootstrap != 0;
}
}

CharaAnimationState::CharaAnimationState(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

void CharaAnimationState::Invalidate() noexcept
{
    fighters_ = {};
    topology_ = {};
    round_generation_ = 0;
    bound_ = false;
}

bool CharaAnimationState::capture_topology(
    std::size_t player, PlayerTopology& output) noexcept
{
    output = {};
    const auto fighter = fighters_[player];
    const auto clip = fighter + chara_anim_clip_player_offset;
    const auto cue_owner = fighter + pose_event_cue_owner_offset;
    std::uint64_t native_count{};
    if (!read_value(memory_, fighter + chara_anim_slot_controller_offset,
            output.packed_data) || output.packed_data == 0)
    {
        topology_issue_ = CharaAnimationTopologyIssue::PackedData;
        return false;
    }
    if (!read_value(memory_, cue_owner, output.cue_owner_vtable)
        || output.cue_owner_vtable == 0
        || !read_value(memory_, cue_owner + cue_owner_enst_offset,
            output.enst_data)
        || !read_value(memory_, cue_owner + cue_owner_scheduler_offset,
            output.scheduler)
        || output.scheduler == 0)
    {
        topology_issue_ = CharaAnimationTopologyIssue::CueOwner;
        return false;
    }
    if (!read_value(memory_, output.scheduler, output.scheduler_vtable)
        || output.scheduler_vtable == 0)
    {
        topology_issue_ = CharaAnimationTopologyIssue::Scheduler;
        return false;
    }
    if (!read_value(memory_, output.scheduler + 8, output.scheduler_chara))
    {
        topology_issue_ = CharaAnimationTopologyIssue::Scheduler;
        return false;
    }
    if (!read_value(memory_, output.scheduler + scheduler_list_head_offset,
            output.list_head)
        || output.list_head == 0
        || !read_value(memory_, output.scheduler + scheduler_list_count_offset,
            native_count)
        || native_count > chara_anim_maximum_triggers
        || !read_value(memory_, output.list_head, output.list_head_next)
        || !read_value(memory_, output.list_head + 8,
            output.list_head_previous))
    {
        topology_issue_ = CharaAnimationTopologyIssue::TriggerList;
        return false;
    }
    output.trigger_count = static_cast<std::uint32_t>(native_count);
    auto current = output.list_head_next;
    auto previous = output.list_head;
    for (std::uint32_t index = 0; index < output.trigger_count; ++index)
    {
        if (current == 0 || current == output.list_head)
        {
            topology_issue_ = CharaAnimationTopologyIssue::TriggerNode;
            return false;
        }
        for (std::uint32_t prior = 0; prior < index; ++prior)
            if (output.triggers[prior].node == current)
            {
                topology_issue_ = CharaAnimationTopologyIssue::TriggerNode;
                return false;
            }
        auto& trigger = output.triggers[index];
        trigger.node = current;
        if (!read_value(memory_, current, trigger.next)
            || !read_value(memory_, current + 8, trigger.previous)
            || !read_value(memory_, current + 0x10, trigger.object)
            || !read_value(memory_, current + 0x18, trigger.control)
            || trigger.previous != previous || trigger.next == 0
            || trigger.object == 0 || trigger.control == 0
            || !read_value(memory_, trigger.object, trigger.object_vtable)
            || trigger.object_vtable == 0)
        {
            topology_issue_ = CharaAnimationTopologyIssue::TriggerNode;
            return false;
        }
        previous = current;
        current = trigger.next;
    }
    const bool closed = current == output.list_head
        && (output.trigger_count == 0
            ? output.list_head_next == output.list_head
                && output.list_head_previous == output.list_head
            : output.list_head_previous == previous);
    if (!closed) topology_issue_ = CharaAnimationTopologyIssue::TriggerList;
    return closed;
}

Status CharaAnimationState::Bind(
    const std::array<std::uintptr_t, 2>& fighters,
    std::uint64_t round_generation) noexcept
{
    Invalidate();
    topology_issue_ = CharaAnimationTopologyIssue::None;
    if (fighters[0] == 0 || fighters[1] == 0 || round_generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    fighters_ = fighters;
    round_generation_ = round_generation;
    if (!capture_topology(0, topology_[0])
        || !capture_topology(1, topology_[1]))
    {
        fighters_ = {};
        topology_ = {};
        round_generation_ = 0;
        bound_ = false;
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    bound_ = true;
    return Status::success();
}

bool CharaAnimationState::topology_matches() noexcept
{
    if (!bound_) return false;
    for (std::size_t player = 0; player < 2; ++player)
    {
        PlayerTopology observed{};
        if (!capture_topology(player, observed)
            || observed.packed_data != topology_[player].packed_data
            || observed.cue_owner_vtable != topology_[player].cue_owner_vtable
            || observed.enst_data != topology_[player].enst_data
            || observed.scheduler != topology_[player].scheduler
            || observed.scheduler_vtable != topology_[player].scheduler_vtable
            || observed.scheduler_chara != topology_[player].scheduler_chara
            || observed.list_head != topology_[player].list_head
            || observed.list_head_next != topology_[player].list_head_next
            || observed.list_head_previous
                != topology_[player].list_head_previous
            || observed.trigger_count != topology_[player].trigger_count)
            return false;
        for (std::uint32_t index = 0; index < observed.trigger_count; ++index)
        {
            if (observed.triggers[index].node
                    != topology_[player].triggers[index].node
                || observed.triggers[index].next
                    != topology_[player].triggers[index].next
                || observed.triggers[index].previous
                    != topology_[player].triggers[index].previous
                || observed.triggers[index].object
                    != topology_[player].triggers[index].object
                || observed.triggers[index].control
                    != topology_[player].triggers[index].control
                || observed.triggers[index].object_vtable
                    != topology_[player].triggers[index].object_vtable)
                return false;
        }
    }
    return true;
}

bool CharaAnimationState::identify_section(std::size_t player,
    std::uintptr_t pointer, PackedSectionIdentity& output) noexcept
{
    output = {};
    if (pointer == 0) return true;
    const auto base = topology_[player].packed_data;
    std::array<std::uint32_t, 5> header{};
    if (!memory_.Read(base, std::as_writable_bytes(std::span{header}))
        || header[0] != 3 || header[3] == header[4]) return false;
    const auto table = base + header[packed_section_table_offset_index];
    std::uint32_t count{};
    if (!read_value(memory_, table, count)
        || count > chara_anim_maximum_packed_sections) return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t begin{}, end{};
        if (!read_value(memory_, table + (index + 1) * 4, begin)
            || !read_value(memory_, table + (index + 2) * 4, end))
            return false;
        if (begin != end && table + begin == pointer)
        {
            std::array<std::byte, 0x10> readable_header{};
            if (!memory_.Read(pointer, readable_header)) return false;
            output.index = index;
            output.present = true;
            return true;
        }
    }
    return false;
}

bool CharaAnimationState::resolve_section(std::size_t player,
    PackedSectionIdentity identity, std::uintptr_t& output) noexcept
{
    output = 0;
    if (!identity.present) return identity.index == 0;
    if (identity.index >= chara_anim_maximum_packed_sections) return false;
    const auto base = topology_[player].packed_data;
    std::array<std::uint32_t, 5> header{};
    if (!memory_.Read(base, std::as_writable_bytes(std::span{header}))
        || header[0] != 3 || header[3] == header[4]) return false;
    const auto table = base + header[packed_section_table_offset_index];
    std::uint32_t count{}, begin{}, end{};
    if (!read_value(memory_, table, count)
        || count > chara_anim_maximum_packed_sections
        || identity.index >= count
        || !read_value(memory_, table + (identity.index + 1) * 4, begin)
        || !read_value(memory_, table + (identity.index + 2) * 4, end)
        || begin == end) return false;
    output = table + begin;
    std::array<std::byte, 0x10> readable_header{};
    return memory_.Read(output, readable_header);
}

bool CharaAnimationState::Validate(
    const CharaAnimationStateImage& image) noexcept
{
    if (image.round_generation == 0) return false;
    for (const auto& player : image.players)
    {
        const bool runtime_scalars_clear = std::all_of(
            player.runtime_scalars.begin(), player.runtime_scalars.end(),
            [](std::byte value) { return value == std::byte{}; });
        if (player.trigger_count > chara_anim_maximum_triggers
            || (!player.clip_section.present && player.clip_section.index != 0)
            || (!player.runtime_section.present
                && player.runtime_section.index != 0)
            || player.clip_section.index >= chara_anim_maximum_packed_sections
            || player.runtime_section.index
                >= chara_anim_maximum_packed_sections
            || (clip_is_active(player) && player.runtime_section.present
                && player.runtime_section != player.clip_section)
            || (clip_is_active(player) && clip_is_bootstrapped(player)
                && player.runtime_section != player.clip_section)
            || (clip_is_active(player) && !player.runtime_section.present
                && !runtime_scalars_clear)
            || (!clip_is_active(player)
                && (player.runtime_section.present
                    || !runtime_scalars_clear))) return false;
    }
    return true;
}

Status CharaAnimationState::capture_unchecked(
    CharaAnimationStateImage& output) noexcept
{
    output = {};
    topology_issue_ = CharaAnimationTopologyIssue::None;
    topology_observed_ = 0;
    if (!topology_matches())
        return Status::failure(FailureCode::IdentityMismatch);
    output.round_generation = round_generation_;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto fighter = fighters_[player];
        const auto clip = fighter + chara_anim_clip_player_offset;
        const auto runtime = fighter + chara_anim_runtime_offset;
        const auto owner = fighter + pose_event_cue_owner_offset;
        std::uintptr_t clip_pointer{}, runtime_pointer{};
        std::uintptr_t clip_owner{};
        std::uintptr_t scheduler_chara{};
        auto& target = output.players[player];
        target.trigger_count = topology_[player].trigger_count;
        if (!read_value(memory_, clip, clip_owner)
            || (clip_owner != 0 && clip_owner != fighter)
            || !read_value(memory_, clip + clip_binding_offset, clip_pointer)
            || !identify_section(player, clip_pointer, target.clip_section))
        {
            topology_issue_ = CharaAnimationTopologyIssue::ClipSection;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (!memory_.Read(clip + clip_scalar_offset, target.clip_scalars))
        {
            topology_issue_ = CharaAnimationTopologyIssue::ClipScalars;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (clip_is_active(target)
            && (!read_value(memory_, runtime, runtime_pointer)
                || !identify_section(player, runtime_pointer,
                    target.runtime_section)
                || (runtime_pointer != 0 && runtime_pointer != clip_pointer)
                || (clip_is_bootstrapped(target)
                    && runtime_pointer != clip_pointer)))
        {
            topology_issue_ = CharaAnimationTopologyIssue::RuntimeSection;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (clip_is_active(target)
            && !memory_.Read(runtime + 8, target.runtime_scalars))
        {
            topology_issue_ = CharaAnimationTopologyIssue::RuntimeScalars;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (!memory_.Read(owner + cue_owner_scalar_offset,
                target.cue_owner_scalars))
        {
            topology_issue_ = CharaAnimationTopologyIssue::CueScalars;
            return Status::failure(FailureCode::CaptureFailed);
        }
        if (!read_value(memory_, topology_[player].scheduler + 8,
                scheduler_chara)
            || scheduler_chara != topology_[player].scheduler_chara)
        {
            topology_issue_ = CharaAnimationTopologyIssue::Scheduler;
            return Status::failure(FailureCode::CaptureFailed);
        }
        target.scheduler_chara_bound = scheduler_chara != 0;
        if (!memory_.Read(topology_[player].scheduler + scheduler_scalar_offset,
                target.scheduler_scalars))
        {
            topology_issue_ = CharaAnimationTopologyIssue::SchedulerScalars;
            return Status::failure(FailureCode::CaptureFailed);
        }
        target.clip_owner_bound = clip_owner != 0;
        for (std::uint32_t index = 0; index < target.trigger_count; ++index)
        {
            if (!memory_.Read(topology_[player].triggers[index].object
                    + trigger_scalar_offset,
                target.trigger_scalars[index]))
            {
                topology_issue_ = CharaAnimationTopologyIssue::TriggerScalars;
                return Status::failure(FailureCode::CaptureFailed);
            }
        }
    }
    return Status::success();
}

Status CharaAnimationState::Capture(CharaAnimationStateImage& output) noexcept
{
    return bound_ ? capture_unchecked(output)
        : Status::failure(FailureCode::AdapterUnqualified);
}

bool CharaAnimationState::write_unchecked(
    const CharaAnimationStateImage& image) noexcept
{
    if (!Validate(image) || image.round_generation != round_generation_
        || image.players[0].trigger_count != topology_[0].trigger_count
        || image.players[1].trigger_count != topology_[1].trigger_count
        || !topology_matches()) return false;
    for (std::size_t player = 0; player < 2; ++player)
    {
        const auto fighter = fighters_[player];
        const auto clip = fighter + chara_anim_clip_player_offset;
        const auto runtime = fighter + chara_anim_runtime_offset;
        const auto owner = fighter + pose_event_cue_owner_offset;
        const auto& source = image.players[player];
        std::uintptr_t clip_pointer{}, runtime_pointer{};
        std::uint32_t current_active{};
        if (!resolve_section(player, source.clip_section, clip_pointer)
            || (clip_is_active(source)
                && (!resolve_section(player, source.runtime_section,
                        runtime_pointer)
                    || (runtime_pointer != 0
                        && runtime_pointer != clip_pointer)
                    || (clip_is_bootstrapped(source)
                        && runtime_pointer != clip_pointer)))
            || !read_value(memory_, clip + 0x28, current_active)
            || !write_value(memory_, clip,
                source.clip_owner_bound ? fighter : std::uintptr_t{})
            || !write_value(memory_, clip + clip_binding_offset, clip_pointer)
            || !memory_.Write(clip + clip_scalar_offset, source.clip_scalars))
            return false;
        if (clip_is_active(source))
        {
            if (!write_value(memory_, runtime, runtime_pointer)
                || !memory_.Write(runtime + 8, source.runtime_scalars))
                return false;
        }
        else if (current_active != 0)
        {
            const std::array<std::byte, 8> cleared{};
            if (!write_value(memory_, runtime, std::uintptr_t{})
                || !memory_.Write(runtime + 8, cleared)) return false;
        }
        if (!memory_.Write(owner + cue_owner_scalar_offset,
                source.cue_owner_scalars)
            || !write_value(memory_, topology_[player].scheduler + 8,
                source.scheduler_chara_bound
                    ? topology_[player].scheduler_chara : std::uintptr_t{})
            || !memory_.Write(topology_[player].scheduler
                    + scheduler_scalar_offset,
                source.scheduler_scalars)) return false;
        for (std::uint32_t index = 0; index < source.trigger_count; ++index)
        {
            if (!memory_.Write(topology_[player].triggers[index].object
                    + trigger_scalar_offset,
                source.trigger_scalars[index])) return false;
        }
    }
    return true;
}

Status CharaAnimationState::RestoreTransactional(
    const CharaAnimationStateImage& image) noexcept
{
    if (!Validate(image) || image.round_generation != round_generation_
        || image.players[0].trigger_count != topology_[0].trigger_count
        || image.players[1].trigger_count != topology_[1].trigger_count
        || !topology_matches())
        return Status::failure(FailureCode::RestorePreflightFailed);
    CharaAnimationStateImage undo{};
    if (!capture_unchecked(undo).ok())
        return Status::failure(FailureCode::CaptureFailed);
    if (write_unchecked(image))
    {
        CharaAnimationStateImage observed{};
        if (capture_unchecked(observed).ok() && observed == image)
            return Status::success();
    }
    const bool undone = write_unchecked(undo);
    CharaAnimationStateImage verified{};
    if (!undone || !capture_unchecked(verified).ok() || verified != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(FailureCode::RestoreWriteFailed);
}

std::vector<std::byte> CharaAnimationState::CanonicalBytes(
    const CharaAnimationStateImage& image)
{
    std::vector<std::byte> output;
    output.reserve(0x1000);
    append(output, &image.round_generation, sizeof(image.round_generation));
    for (const auto& player : image.players)
    {
        const std::uint8_t clip_owner_bound = player.clip_owner_bound ? 1 : 0;
        append(output, &clip_owner_bound, sizeof(clip_owner_bound));
        append(output, &player.clip_section.index,
            sizeof(player.clip_section.index));
        const std::uint8_t clip_present = player.clip_section.present ? 1 : 0;
        append(output, &clip_present, sizeof(clip_present));
        append(output, player.clip_scalars.data(), player.clip_scalars.size());
        append(output, &player.runtime_section.index,
            sizeof(player.runtime_section.index));
        const std::uint8_t runtime_present =
            player.runtime_section.present ? 1 : 0;
        append(output, &runtime_present, sizeof(runtime_present));
        append(output, player.runtime_scalars.data(),
            player.runtime_scalars.size());
        append(output, player.cue_owner_scalars.data(),
            player.cue_owner_scalars.size());
        const std::uint8_t scheduler_chara_bound =
            player.scheduler_chara_bound ? 1 : 0;
        append(output, &scheduler_chara_bound,
            sizeof(scheduler_chara_bound));
        append(output, player.scheduler_scalars.data(),
            player.scheduler_scalars.size());
        append(output, &player.trigger_count, sizeof(player.trigger_count));
        append(output, player.trigger_scalars.data(),
            player.trigger_count * sizeof(player.trigger_scalars[0]));
    }
    return output;
}

Status CharaAnimationState::DecodeCanonicalBytes(
    std::span<const std::byte> bytes, CharaAnimationStateImage& output) noexcept
{
    output = {};
    std::size_t cursor{};
    const auto take = [&bytes, &cursor](void* destination, std::size_t size) {
        if (cursor > bytes.size() || size > bytes.size() - cursor) return false;
        std::memcpy(destination, bytes.data() + cursor, size);
        cursor += size;
        return true;
    };
    if (!take(&output.round_generation, sizeof(output.round_generation)))
        return Status::failure(FailureCode::CaptureFailed);
    for (auto& player : output.players)
    {
        std::uint8_t clip_owner_bound{}, clip_present{}, runtime_present{};
        std::uint8_t scheduler_chara_bound{};
        if (!take(&clip_owner_bound, sizeof(clip_owner_bound))
            || clip_owner_bound > 1
            || !take(&player.clip_section.index,
                sizeof(player.clip_section.index))
            || !take(&clip_present, sizeof(clip_present)) || clip_present > 1
            || !take(player.clip_scalars.data(), player.clip_scalars.size())
            || !take(&player.runtime_section.index,
                sizeof(player.runtime_section.index))
            || !take(&runtime_present, sizeof(runtime_present))
            || runtime_present > 1
            || !take(player.runtime_scalars.data(),
                player.runtime_scalars.size())
            || !take(player.cue_owner_scalars.data(),
                player.cue_owner_scalars.size())
            || !take(&scheduler_chara_bound,
                sizeof(scheduler_chara_bound))
            || scheduler_chara_bound > 1
            || !take(player.scheduler_scalars.data(),
                player.scheduler_scalars.size())
            || !take(&player.trigger_count, sizeof(player.trigger_count))
            || player.trigger_count > chara_anim_maximum_triggers
            || !take(player.trigger_scalars.data(),
                player.trigger_count * sizeof(player.trigger_scalars[0])))
            return Status::failure(FailureCode::CaptureFailed);
        player.clip_owner_bound = clip_owner_bound != 0;
        player.clip_section.present = clip_present != 0;
        player.runtime_section.present = runtime_present != 0;
        player.scheduler_chara_bound = scheduler_chara_bound != 0;
    }
    return cursor == bytes.size() && Validate(output)
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}
}
