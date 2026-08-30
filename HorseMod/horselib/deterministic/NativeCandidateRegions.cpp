#include "NativeCandidateRegions.hpp"
#include "LocalImageChecksum.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace Horse::Deterministic
{
namespace
{
void reset_native_candidate(NativeCandidateImage& output) noexcept
{
    auto wind_emitter_states =
        std::move(output.stage_wind_emitters.states);
    output = {};
    output.stage_wind_emitters.states = std::move(wind_emitter_states);
    output.stage_wind_emitters.states.clear();
}
struct Range
{
    std::size_t offset;
    std::size_t size;
};

constexpr std::array<std::size_t, 6> pump_identity_offsets{
    0x00, 0x08, 0x10, 0x18, 0x40, 0x48};
constexpr std::array<std::size_t, 17> move_command_identity_offsets{
    0x0008, 0x0010, 0x0028, 0x0030, 0x0340, 0x0BA8,
    0x0BB0, 0x0BB8, 0x0BC0, 0x0BC8, 0x0BD0, 0x0BD8,
    0x0BE0, 0x0CC8, 0x0CD8, 0x0CE0, 0x1998};
constexpr std::array<Range, 9> move_command_ranges{{
    {0x0000, 0x0008},
    {0x0018, 0x0010},
    {0x0038, 0x0308},
    {0x0348, 0x0860},
    {0x0BE8, 0x00E0},
    {0x0CD0, 0x0008},
    {0x0CE8, 0x0CB0},
    {0x19A0, 0x1088},
    {0x2AA8, 0x058C},
}};
constexpr std::array<std::size_t, 4> vfx_edge_diagnostic_offsets{
    0x4E8, 0x630, 0x510, 0x658};
constexpr std::ptrdiff_t movevm_state_shorts_offset = 0x197C;
constexpr std::size_t camera_action_stride = 0x3E0;
constexpr std::size_t camera_distance_history_offset = 0x25C;
constexpr std::ptrdiff_t camera_fighter_render_position_offset = 0x2090;
constexpr std::uint32_t player_watch_camera_vtable_rva = 0x3E87EB0;
constexpr std::array<Range, 46> camera_component_common_ranges{{
    {0x008,0x04},{0x00C,0x04},{0x010,0x04},{0x014,0x04},
    {0x020,0x10},{0x030,0x04},{0x040,0x10},{0x050,0x10},
    {0x060,0x10},{0x070,0x04},{0x074,0x04},{0x078,0x04},
    {0x07C,0x04},{0x080,0x04},{0x084,0x04},{0x088,0x04},
    {0x08C,0x04},{0x090,0x04},{0x094,0x04},{0x098,0x04},
    {0x09C,0x04},{0x0A0,0x10},{0x0B0,0x04},{0x0B4,0x04},
    {0x0B8,0x04},{0x0C0,0x10},{0x0D0,0x04},{0x0D4,0x04},
    {0x0E0,0x10},{0x0F0,0x10},{0x100,0x04},{0x110,0x10},
    {0x120,0x04},{0x130,0x10},{0x140,0x04},{0x144,0x10},
    {0x158,0x04},
    {0x15C,0x04},{0x160,0x10},{0x170,0x10},{0x180,0x04},
    {0x184,0x04},
    // LuxEffectCamera_UpdateSpringState advances these persistent limits,
    // velocities, and accumulators after every class update.  The native
    // archive omits them even though the next tick consumes them to produce
    // +0x50/+0x54/+0x70 and the published camera position.
    {0x188,0x08},{0x190,0x08},{0x1A0,0x08},{0x1B0,0x18},
}};
constexpr std::array<Range, 10> player_watch_active_ranges{{
    {0x1D0,0x0C},{0x1E0,0x0C},{0x1F0,0x0C},{0x200,0x0C},
    {0x210,0x14},{0x230,0x0C},{0x240,0x0C},{0x250,0x08},
    // Persistent terrain-zone factor. UpdateTerrainZoneFactor reuses it when
    // the selected character has the +0x4450E override, then the roll update
    // consumes it to advance the common +0x50 angle.
    {0x258,0x04},
    {0x25C,0x54},
}};
constexpr std::uint16_t player_watch_active_bytes = 0xBC;
constexpr std::uint32_t camera_serialize_base_rva = 0x33FBC0;
constexpr std::uint32_t camera_serialize_state_buffer_rva = 0x318860;
constexpr std::uint32_t camera_serialize_state_rva = 0x31C190;
constexpr std::uint32_t camera_serialize_chara_reference_rva = 0x317BD0;
constexpr std::uint32_t camera_serialize_attention_rva = 0x340ED0;
constexpr std::uint32_t camera_serialize_stay_rva = 0x341110;
constexpr std::ptrdiff_t manager_input_log = 0x478;
constexpr std::ptrdiff_t manager_repeat_pending = 0x1462;
constexpr std::ptrdiff_t manager_pending_move_state = 0x1463;
constexpr std::ptrdiff_t manager_pending_dispatch = 0x1464;
constexpr std::ptrdiff_t manager_round_image_applied = 0x1465;
constexpr std::ptrdiff_t manager_round_sequence_array = 0x1470;
constexpr std::ptrdiff_t manager_round_sequence_count = 0x1478;
constexpr std::ptrdiff_t manager_round_sequence_capacity = 0x147C;
constexpr std::ptrdiff_t manager_round_sequence_state = 0x1480;
constexpr std::ptrdiff_t manager_game_round_cursor = 0x1488;
constexpr std::ptrdiff_t manager_game_time_cursor = 0x148C;
constexpr std::ptrdiff_t manager_round_state_frame = 0x1490;
constexpr std::ptrdiff_t manager_previous_input_array = 0x1498;
constexpr std::ptrdiff_t manager_previous_input_count = 0x14A0;
constexpr std::ptrdiff_t manager_previous_input_capacity = 0x14A4;
constexpr std::ptrdiff_t manager_input_pair_array = 0x14A8;
constexpr std::ptrdiff_t manager_active_player_count = 0x14B0;
constexpr std::ptrdiff_t manager_input_pair_capacity = 0x14B4;
constexpr std::ptrdiff_t manager_prior_input_pair_array = 0x14B8;
constexpr std::ptrdiff_t manager_prior_input_pair_count = 0x14C0;
constexpr std::ptrdiff_t manager_prior_input_pair_capacity = 0x14C4;
constexpr std::ptrdiff_t manager_unpause_countdown = 0x14F0;
constexpr std::ptrdiff_t input_log_game_round = 0x3A0;
constexpr std::ptrdiff_t input_log_game_time = 0x3A4;
constexpr std::ptrdiff_t input_log_class = 0x10;
constexpr std::ptrdiff_t input_log_semantic_begin = 0x390;
constexpr std::ptrdiff_t input_log_cache_begin = 0x3C0;
constexpr std::size_t input_log_cache_row_stride = 0x10;
constexpr std::array<std::pair<std::uint32_t, std::uint8_t>, 77> subvm_classes{{
    {0x3E863D0,0x68},{0x3E85608,0x78},{0x3E868F0,0x78},
    {0x3E85698,0x78},{0x3E85D10,0x68},{0x3E857B8,0x68},
    {0x3E86C50,0x68},{0x3E86BC0,0x68},{0x3E865D8,0x68},
    {0x3E860B8,0x68},{0x3E85C38,0x70},{0x3E862F8,0x68},
    {0x3E85C80,0x68},{0x3E85770,0x68},{0x3E86418,0x68},
    {0x3E85E30,0x68},{0x3E86028,0x78},{0x3E85F08,0x78},
    {0x3E861D8,0x78},{0x3E86788,0x68},{0x3E86D28,0x68},
    {0x3E86FF8,0x68},{0x3E86818,0x70},{0x3E85D58,0x68},
    {0x3E86190,0x68},{0x3E864F0,0x68},{0x3E85A40,0x68},
    {0x3E86B78,0x78},{0x3E86938,0x68},{0x3E86548,0x68},
    {0x3E858D8,0x68},{0x3E85848,0x68},{0x3E866B0,0x78},
    {0x3E86100,0x78},{0x3E85DA0,0x78},{0x3E85578,0x78},
    {0x3E85CC8,0x78},{0x3E85F50,0x68},{0x3E86860,0x68},
    {0x3E859F8,0x70},{0x3E868A8,0x68},{0x3E86F68,0x68},
    {0x3E85BA8,0x68},{0x3E856E0,0x68},{0x3E85AD0,0x68},
    {0x3E864A8,0x68},{0x3E85DE8,0x68},{0x3E86668,0x68},
    {0x3E867D0,0x68},{0x3E86070,0x78},{0x3E869C8,0x78},
    {0x3E86D70,0x70},{0x3E85A88,0x78},{0x3E85B18,0x78},
    {0x3E86C98,0x68},{0x3E859B0,0x68},{0x3E86E90,0x68},
    {0x3E86DB8,0x68},{0x3E86E48,0x68},{0x3E866F8,0x68},
    {0x3E85E78,0x68},{0x3E86CE0,0x68},{0x3E86620,0x68},
    {0x3E86220,0x78},{0x3E85920,0x78},{0x3E86B30,0x68},
    {0x3E855C0,0x68},{0x3E86A10,0x68},{0x3E86340,0x68},
    {0x3E86590,0x68},{0x3E86E00,0x68},{0x3E86268,0x68},
    {0x3E86FB0,0x68},{0x3E85BF0,0x68},{0x3E891B8,0x80},
    {0x3E89248,0x80},{0x3E85EC0,0x70},
}};

static_assert([] {
    std::size_t total = 0;
    for (const auto range : move_command_ranges) total += range.size;
    return total == native_move_command_semantic_bytes;
}());
static_assert([] {
    std::size_t total{};
    for (const auto range : camera_component_common_ranges) total += range.size;
    return total == native_camera_component_common_bytes;
}());
static_assert([] {
    std::size_t total{};
    for (const auto range : player_watch_active_ranges) total += range.size;
    return total == player_watch_active_bytes;
}());

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& value) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&value, 1}));
}

std::uint8_t extent_for_rva(std::uint32_t rva) noexcept
{
    const auto found = std::find_if(
        subvm_classes.begin(), subvm_classes.end(),
        [rva](const auto& item) { return item.first == rva; });
    return found == subvm_classes.end() ? 0 : found->second;
}

std::size_t derived_size(std::uint8_t extent) noexcept
{
    if (extent == 0x80) return 0x14;
    return extent > 0x68 ? extent - 0x68 : 0;
}

void append_bytes(std::vector<std::byte>& output, const void* data, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(data);
    output.insert(output.end(), first, first + size);
}

struct FingerprintAccumulator
{
    void Add(const void* data, std::size_t size) noexcept
    {
        checksum.Add(data, size);
    }

    std::uint64_t FinishAndReset() noexcept
    {
        const auto result = checksum.Finish();
        checksum = {};
        return result;
    }

    LocalImageChecksum checksum{};
};

void append_bytes(
    FingerprintAccumulator& output, const void* data, std::size_t size)
{
    output.Add(data, size);
}

void append_frame_boundary(
    std::vector<std::byte>& output,
    const NativeFrameBoundaryImage& frame)
{
    append_bytes(output, &frame.frame_counter, sizeof(frame.frame_counter));
    append_bytes(output, &frame.input_game_round, sizeof(frame.input_game_round));
    append_bytes(output, &frame.input_game_time, sizeof(frame.input_game_time));
    append_bytes(output, &frame.manager_game_round_cursor,
        sizeof(frame.manager_game_round_cursor));
    append_bytes(output, &frame.manager_game_time_cursor,
        sizeof(frame.manager_game_time_cursor));
    append_bytes(output, &frame.round_state_frame, sizeof(frame.round_state_frame));
    append_bytes(output, &frame.unpause_countdown, sizeof(frame.unpause_countdown));
    append_bytes(output, frame.previous_inputs.data(), sizeof(frame.previous_inputs));
    append_bytes(output, frame.input_pairs.data(), sizeof(frame.input_pairs));
    append_bytes(output, frame.prior_input_pairs.data(),
        sizeof(frame.prior_input_pairs));
    append_bytes(output, &frame.repeat_pending, sizeof(frame.repeat_pending));
    append_bytes(output, &frame.pending_move_state,
        sizeof(frame.pending_move_state));
    append_bytes(output, &frame.pending_dispatch,
        sizeof(frame.pending_dispatch));
    append_bytes(output, &frame.round_image_applied,
        sizeof(frame.round_image_applied));
}

void append_round_sequence(
    std::vector<std::byte>& output,
    const NativeRoundSequenceImage& sequence)
{
    append_bytes(output, &sequence.count, sizeof(sequence.count));
    append_bytes(output, &sequence.current_state,
        sizeof(sequence.current_state));
    append_bytes(output, sequence.states.data(), sequence.count);
}

void append_round_sequence(
    FingerprintAccumulator& output,
    const NativeRoundSequenceImage& sequence)
{
    append_bytes(output, &sequence.count, sizeof(sequence.count));
    append_bytes(output, &sequence.current_state,
        sizeof(sequence.current_state));
    append_bytes(output, sequence.states.data(), sequence.count);
}

void append_input_log(
    std::vector<std::byte>& output,
    const NativeFrameInputLogImage& input_log)
{
    constexpr std::size_t packed_row_size = sizeof(std::int32_t)
        + sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint8_t);
    const auto begin = output.size();
    output.resize(begin + input_log.scalars.size()
        + input_log.cache_rows.size() * packed_row_size);
    auto* destination = output.data() + begin;
    std::memcpy(destination, input_log.scalars.data(), input_log.scalars.size());
    destination += input_log.scalars.size();
    for (const auto& row : input_log.cache_rows)
    {
        std::memcpy(destination, &row.game_round, sizeof(row.game_round));
        destination += sizeof(row.game_round);
        std::memcpy(destination, &row.frame_index, sizeof(row.frame_index));
        destination += sizeof(row.frame_index);
        std::memcpy(destination, &row.input_value, sizeof(row.input_value));
        destination += sizeof(row.input_value);
        std::memcpy(destination, &row.filled, sizeof(row.filled));
        destination += sizeof(row.filled);
    }
}

bool valid_pending_hit(const NativePendingHitImage& image) noexcept
{
    return image.attacker_slot <= 2 && image.launcher_sync <= 1;
}

bool valid_camera_history(
    const NativeCameraDistanceHistoryImage& image) noexcept
{
    if (image.present > 1) return false;
    if (image.present == 0)
    {
        return image.sample_count == 0 && image.cursor == 0
            && std::all_of(image.sample_bits.begin(), image.sample_bits.end(),
                [](std::uint32_t value) { return value == 0; });
    }
    return image.sample_count >= 0 && image.cursor < image.sample_bits.size();
}

NativeCameraComponentSerialization camera_serialization_for_identity(
    std::uintptr_t image_base, std::uintptr_t vtable,
    std::uintptr_t writer) noexcept
{
    if (vtable == image_base + player_watch_camera_vtable_rva)
        return NativeCameraComponentSerialization::PlayerWatchActive;
    if (writer == image_base + camera_serialize_base_rva)
        return NativeCameraComponentSerialization::Base;
    if (writer == image_base + camera_serialize_state_buffer_rva)
        return NativeCameraComponentSerialization::StateBuffer;
    if (writer == image_base + camera_serialize_state_rva)
        return NativeCameraComponentSerialization::State;
    if (writer == image_base + camera_serialize_chara_reference_rva)
        return NativeCameraComponentSerialization::CharaReference;
    if (writer == image_base + camera_serialize_attention_rva)
        return NativeCameraComponentSerialization::Attention;
    if (writer == image_base + camera_serialize_stay_rva)
        return NativeCameraComponentSerialization::Stay;
    return NativeCameraComponentSerialization::None;
}

std::uint16_t camera_derived_size(
    NativeCameraComponentSerialization serialization) noexcept
{
    switch (serialization)
    {
    case NativeCameraComponentSerialization::StateBuffer: return 0x140;
    case NativeCameraComponentSerialization::State: return 0x1C;
    case NativeCameraComponentSerialization::CharaReference: return 0x14;
    case NativeCameraComponentSerialization::Attention: return 0x10;
    case NativeCameraComponentSerialization::Stay: return 0x0C;
    case NativeCameraComponentSerialization::PlayerWatchActive:
        return player_watch_active_bytes;
    default: return 0;
    }
}

bool valid_camera_component(const NativeCameraComponentImage& image) noexcept
{
    if (image.present > 1) return false;
    if (image.present == 0)
        return image.serialization == NativeCameraComponentSerialization::None
            && image.vtable_rva == 0 && image.writer_rva == 0
            && image.derived_size == 0 && image.tracked_chara_slot == -1;
    return image.serialization != NativeCameraComponentSerialization::None
        && image.vtable_rva != 0 && image.writer_rva != 0
        && image.derived_size == camera_derived_size(image.serialization)
        && image.derived_size <= image.derived.size()
        && image.tracked_chara_slot >= -1 && image.tracked_chara_slot <= 1
        && (image.serialization == NativeCameraComponentSerialization::CharaReference
            || image.tracked_chara_slot == -1);
}

bool capture_input_log_cache(
    INativeMemory& memory,
    std::uintptr_t input_log,
    NativeFrameInputLogImage& output,
    NativeCandidateValidationDiagnostic& diagnostic) noexcept
{
    if (!memory.Read(input_log + input_log_semantic_begin, output.scalars))
    {
        diagnostic.issue = NativeCandidateValidationIssue::InputLogScalarRead;
        return false;
    }
    std::array<std::byte,
        input_log_cache_row_stride * 1024> raw{};
    if (!memory.Read(input_log + input_log_cache_begin, raw))
    {
        diagnostic.issue = NativeCandidateValidationIssue::InputLogCacheRead;
        return false;
    }
    for (std::size_t index = 0; index < output.cache_rows.size(); ++index)
    {
        const std::byte* source = raw.data() + index * input_log_cache_row_stride;
        auto& row = output.cache_rows[index];
        std::memcpy(&row.game_round, source, sizeof(row.game_round));
        std::memcpy(&row.frame_index, source + 4, sizeof(row.frame_index));
        std::memcpy(&row.input_value, source + 8, sizeof(row.input_value));
        std::memcpy(&row.filled, source + 12, sizeof(row.filled));
        if (row.filled > 1)
        {
            diagnostic.issue = NativeCandidateValidationIssue::InputLogCacheFill;
            diagnostic.index = static_cast<std::uint32_t>(index);
            diagnostic.observed_a = row.filled;
            return false;
        }
    }
    return true;
}

bool validate_input_log_image(
    const NativeFrameInputLogImage& input_log,
    const NativeFrameBoundaryImage& frame,
    NativeCandidateValidationDiagnostic& diagnostic) noexcept
{
    std::int32_t player_count{};
    std::int32_t game_round{};
    std::int32_t game_time{};
    std::memcpy(&player_count, input_log.scalars.data() + 8,
        sizeof(player_count));
    std::memcpy(&game_round, input_log.scalars.data() + 0x10,
        sizeof(game_round));
    std::memcpy(&game_time, input_log.scalars.data() + 0x14,
        sizeof(game_time));
    if (player_count != 2)
    {
        diagnostic.issue = NativeCandidateValidationIssue::InputLogPlayerCount;
        diagnostic.observed_a = player_count;
        diagnostic.expected_a = 2;
        return false;
    }
    if (game_round != frame.input_game_round
        || game_time != frame.input_game_time)
    {
        diagnostic.issue = NativeCandidateValidationIssue::InputLogClock;
        diagnostic.observed_a = game_round;
        diagnostic.observed_b = game_time;
        diagnostic.expected_a = frame.input_game_round;
        diagnostic.expected_b = frame.input_game_time;
        return false;
    }
    return std::all_of(
        input_log.cache_rows.begin(), input_log.cache_rows.end(),
        [](const NativeInputCacheRowImage& row) { return row.filled <= 1; });
}

bool write_input_log_cache(
    INativeMemory& memory,
    std::uintptr_t input_log,
    const NativeFrameInputLogImage& image) noexcept
{
    std::array<std::byte,
        input_log_cache_row_stride * 1024> raw{};
    if (!memory.Read(input_log + input_log_cache_begin, raw)) return false;
    for (std::size_t index = 0; index < image.cache_rows.size(); ++index)
    {
        const auto& row = image.cache_rows[index];
        if (row.filled > 1) return false;
        std::byte* destination = raw.data() + index * input_log_cache_row_stride;
        std::memcpy(destination, &row.game_round, sizeof(row.game_round));
        std::memcpy(destination + 4, &row.frame_index, sizeof(row.frame_index));
        std::memcpy(destination + 8, &row.input_value, sizeof(row.input_value));
        std::memcpy(destination + 12, &row.filled, sizeof(row.filled));
    }
    return memory.Write(input_log + input_log_semantic_begin, image.scalars)
        && memory.Write(input_log + input_log_cache_begin, raw);
}
}

NativeCandidateRegions::NativeCandidateRegions(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

bool NativeCandidateRegions::read_bytes(
    std::uintptr_t address, std::span<std::byte> out) noexcept
{
    return address != 0 && !out.empty() && memory_.Read(address, out);
}

bool NativeCandidateRegions::write_bytes(
    std::uintptr_t address, std::span<const std::byte> bytes) noexcept
{
    return address != 0 && !bytes.empty() && memory_.Write(address, bytes);
}

bool NativeCandidateRegions::capture_identities(BoundIdentities& output) noexcept
{
    std::int32_t previous_count{};
    std::int32_t previous_capacity{};
    std::int32_t player_count{};
    std::int32_t input_pair_capacity{};
    std::int32_t prior_count{};
    std::int32_t prior_capacity{};
    std::int32_t round_sequence_count{};
    std::int32_t mask_count{};
    std::int32_t mask_capacity{};
    if (!read_value(memory_, addresses_.battle_manager + manager_input_log,
            output.input_log)
        || output.input_log != addresses_.input_log
        || !read_value(memory_, addresses_.input_log + input_log_class,
            output.input_log_class)
        || output.input_log_class == 0
        || !read_value(memory_, addresses_.battle_manager
                + manager_previous_input_array,
            output.previous_input_array)
        || !read_value(memory_, addresses_.battle_manager
                + manager_previous_input_count,
            previous_count)
        || !read_value(memory_, addresses_.battle_manager
                + manager_previous_input_capacity,
            previous_capacity)
        || !read_value(memory_, addresses_.battle_manager
                + manager_input_pair_array,
            output.input_pair_array)
        || !read_value(memory_, addresses_.battle_manager
                + manager_input_pair_capacity,
            input_pair_capacity)
        || !read_value(memory_, addresses_.battle_manager
                + manager_prior_input_pair_array,
            output.prior_input_pair_array)
        || !read_value(memory_, addresses_.battle_manager
                + manager_prior_input_pair_count,
            prior_count)
        || !read_value(memory_, addresses_.battle_manager
                + manager_prior_input_pair_capacity,
            prior_capacity)
        || !read_value(memory_, addresses_.battle_manager
                + manager_active_player_count,
            player_count)
        || !read_value(memory_, addresses_.battle_manager
                + manager_round_sequence_array,
            output.round_sequence_array)
        || !read_value(memory_, addresses_.battle_manager
                + manager_round_sequence_count,
            round_sequence_count)
        || !read_value(memory_, addresses_.battle_manager
                + manager_round_sequence_capacity,
            output.round_sequence_capacity)
        || !read_value(memory_, addresses_.move_dispatch + 0x4A8,
            output.event_mask_owner)
        || !read_value(memory_, addresses_.move_dispatch + 0x4B0, mask_count)
        || !read_value(memory_, addresses_.move_dispatch + 0x4B4, mask_capacity)
        || output.event_mask_owner == 0 || mask_count != 2 || mask_capacity != 2)
    {
        return false;
    }
    const auto array_invalid = [this](
        std::uint32_t index, std::int32_t count, std::int32_t capacity) {
        validation_diagnostic_.issue = NativeCandidateValidationIssue::IdentityRead;
        validation_diagnostic_.index = index;
        validation_diagnostic_.observed_a = count;
        validation_diagnostic_.observed_b = capacity;
        validation_diagnostic_.expected_a = 2;
        validation_diagnostic_.expected_b = 2;
        return false;
    };
    if (output.previous_input_array == 0
        || previous_count != 2 || previous_capacity < previous_count)
        return array_invalid(20, previous_count, previous_capacity);
    if (output.input_pair_array == 0
        || player_count != 2 || input_pair_capacity < player_count)
        return array_invalid(21, player_count, input_pair_capacity);
    if (output.prior_input_pair_array == 0
        || prior_count != 2 || prior_capacity < prior_count)
        return array_invalid(22, prior_count, prior_capacity);
    if (output.round_sequence_array == 0 || round_sequence_count < 0
        || round_sequence_count
            > static_cast<std::int32_t>(native_round_sequence_max_states)
        || output.round_sequence_capacity < round_sequence_count
        || output.round_sequence_capacity < 8
        || output.round_sequence_capacity > 1024)
    {
        validation_diagnostic_.issue = NativeCandidateValidationIssue::IdentityRead;
        validation_diagnostic_.index = 23;
        validation_diagnostic_.observed_a = round_sequence_count;
        validation_diagnostic_.observed_b = output.round_sequence_capacity;
        validation_diagnostic_.expected_a = 0;
        validation_diagnostic_.expected_b =
            static_cast<std::int32_t>(native_round_sequence_max_states);
        return false;
    }
    for (std::size_t i = 0; i < output.pump.size(); ++i)
    {
        if (!read_value(
                memory_, addresses_.pump_state + pump_identity_offsets[i], output.pump[i]))
        {
            return false;
        }
    }
    for (std::size_t lane = 0; lane < output.sub_vms.size(); ++lane)
    {
        auto& identity = output.sub_vms[lane];
        identity.scheduler = addresses_.scheduler_base + lane * 0x60;
        if (!read_value(memory_, identity.scheduler, identity.scheduler_vtable)
            || !read_value(memory_, identity.scheduler + 0x10, identity.scheduler_fighter)
            || !read_value(memory_, identity.scheduler + 0x50, identity.object)
            || identity.scheduler_vtable == 0 || identity.scheduler_fighter == 0
            || identity.object == 0
            || !read_value(memory_, identity.object, identity.vtable)
            || identity.vtable < addresses_.image_base
            || identity.vtable - addresses_.image_base
                > std::numeric_limits<std::uint32_t>::max()
            || !read_value(memory_, identity.object + 0x10, identity.fighter)
            || !read_value(memory_, identity.object + 0x18, identity.opponent)
            || !read_value(memory_, identity.object + 0x60, identity.owner_scheduler))
        {
            return false;
        }
        identity.extent = extent_for_rva(
            static_cast<std::uint32_t>(identity.vtable - addresses_.image_base));
        if (identity.extent == 0 || identity.fighter == 0 || identity.opponent == 0
            || identity.owner_scheduler != identity.scheduler
            || identity.scheduler_fighter != identity.fighter
            || identity.fighter != addresses_.fighter_roots[lane])
        {
            return false;
        }
    }
    for (std::size_t lane = 0; lane < output.move_commands.size(); ++lane)
    {
        const auto slot = addresses_.move_command_base + lane * 0x3038;
        for (std::size_t i = 0; i < move_command_identity_offsets.size(); ++i)
        {
            if (!read_value(
                    memory_, slot + move_command_identity_offsets[i],
                    output.move_commands[lane][i]))
            {
                return false;
            }
        }
        if (output.move_commands[lane][0] == 0
            || output.move_commands[lane][1] == 0)
        {
            return false;
        }
    }
    if (addresses_.camera_director != 0)
    {
        for (std::size_t index = 0;
             index < output.camera_components.size(); ++index)
        {
            auto& identity = output.camera_components[index];
            if (!read_value(memory_, addresses_.camera_director + 0x270
                    + index * sizeof(std::uintptr_t), identity.object))
            {
                validation_diagnostic_.index = static_cast<std::uint32_t>(
                    100 + index);
                return false;
            }
            if (identity.object == 0) continue;
            if (!read_value(memory_, identity.object, identity.vtable)
                || identity.vtable == 0
                || !read_value(memory_, identity.vtable + 0x100,
                    identity.writer))
            {
                validation_diagnostic_.index = static_cast<std::uint32_t>(
                    120 + index);
                return false;
            }
            identity.serialization = camera_serialization_for_identity(
                addresses_.image_base, identity.vtable, identity.writer);
            if (identity.serialization
                == NativeCameraComponentSerialization::None)
            {
                validation_diagnostic_.index = static_cast<std::uint32_t>(
                    140 + index);
                validation_diagnostic_.observed_a = static_cast<std::int32_t>(
                    identity.vtable - addresses_.image_base);
                validation_diagnostic_.observed_b = static_cast<std::int32_t>(
                    identity.writer - addresses_.image_base);
                return false;
            }
        }
    }
    if (addresses_.camera_action_backing != 0)
    {
        for (std::size_t index = 0; index < output.camera_vtables.size(); ++index)
        {
            if (!read_value(memory_, addresses_.camera_action_backing
                    + index * camera_action_stride,
                    output.camera_vtables[index])
                || output.camera_vtables[index] < addresses_.image_base
                || output.camera_vtables[index] - addresses_.image_base
                    > std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }
        }
    }
    if (!read_value(memory_, addresses_.stage_wind_emitter_list,
            output.stage_wind_emitter_sentinel)
        || output.stage_wind_emitter_sentinel == 0)
    {
        return false;
    }
    std::uintptr_t emitter_node{};
    if (!read_value(memory_, output.stage_wind_emitter_sentinel, emitter_node))
        return false;
    std::uintptr_t previous = output.stage_wind_emitter_sentinel;
    while (emitter_node != output.stage_wind_emitter_sentinel)
    {
        const auto index = output.stage_wind_emitter_count;
        if (index >= native_stage_wind_emitter_max_count) return false;
        std::uintptr_t next{}, node_previous{}, emitter{}, ref_control{};
        if (emitter_node == 0
            || !read_value(memory_, emitter_node, next)
            || !read_value(memory_, emitter_node + 8, node_previous)
            || !read_value(memory_, emitter_node + 0x10, emitter)
            || !read_value(memory_, emitter_node + 0x18, ref_control)
            || node_previous != previous || emitter == 0 || ref_control == 0)
        {
            return false;
        }
        output.stage_wind_emitter_nodes[index] = emitter_node;
        output.stage_wind_emitters[index] = emitter;
        output.stage_wind_emitter_ref_controls[index] = ref_control;
        ++output.stage_wind_emitter_count;
        previous = emitter_node;
        emitter_node = next;
    }
    std::uintptr_t sentinel_previous{};
    if (!read_value(memory_, output.stage_wind_emitter_sentinel + 8,
            sentinel_previous)
        || sentinel_previous != previous)
    {
        return false;
    }
    return true;
}

bool NativeCandidateRegions::identities_match() noexcept
{
    BoundIdentities current{};
    return capture_identities(current)
        && current.input_log == identities_.input_log
        && current.input_log_class == identities_.input_log_class
        && current.previous_input_array == identities_.previous_input_array
        && current.input_pair_array == identities_.input_pair_array
        && current.prior_input_pair_array == identities_.prior_input_pair_array
        && current.round_sequence_array == identities_.round_sequence_array
        && current.round_sequence_capacity == identities_.round_sequence_capacity
        && current.event_mask_owner == identities_.event_mask_owner
        && current.pump == identities_.pump
        && current.move_commands == identities_.move_commands
        && current.camera_components == identities_.camera_components
        && current.camera_vtables == identities_.camera_vtables
        && current.stage_wind_emitter_sentinel
            == identities_.stage_wind_emitter_sentinel
        && current.stage_wind_emitter_nodes
            == identities_.stage_wind_emitter_nodes
        && current.stage_wind_emitters == identities_.stage_wind_emitters
        && current.stage_wind_emitter_ref_controls
            == identities_.stage_wind_emitter_ref_controls
        && current.stage_wind_emitter_count
            == identities_.stage_wind_emitter_count
        && std::equal(
            current.sub_vms.begin(), current.sub_vms.end(), identities_.sub_vms.begin(),
            [](const SubVmIdentity& a, const SubVmIdentity& b) {
                return a.scheduler == b.scheduler && a.object == b.object
                    && a.scheduler_vtable == b.scheduler_vtable
                    && a.scheduler_fighter == b.scheduler_fighter
                    && a.vtable == b.vtable && a.fighter == b.fighter
                    && a.opponent == b.opponent
                    && a.owner_scheduler == b.owner_scheduler && a.extent == b.extent;
            });
}

Status NativeCandidateRegions::Bind(const NativeCandidateAddresses& addresses) noexcept
{
    Invalidate();
    validation_diagnostic_ = {};
    if (addresses.image_base == 0 || addresses.battle_manager == 0
        || addresses.input_log == 0 || addresses.frame_counter == 0
        || addresses.move_dispatch == 0
        || addresses.pump_state == 0 || addresses.scheduler_base == 0
        || addresses.move_command_base == 0 || addresses.slot_param_base == 0
        || addresses.lcg_rng == 0 || addresses.lfsr_rng == 0
        || addresses.xorshift_rng == 0 || addresses.wind_rng == 0
        || addresses.vm_freeze_record == 0
        || addresses.stage_wind_emitter_list == 0
        || addresses.pending_hit_record == 0
        || addresses.pending_launcher_sync == 0
        || addresses.fighter_roots[0] == 0 || addresses.fighter_roots[1] == 0
        || addresses.fighter_roots[0] == addresses.fighter_roots[1]
        || addresses.session_generation == 0 || addresses.round_generation == 0)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    addresses_ = addresses;
    if (!capture_identities(identities_))
    {
        validation_diagnostic_.issue = NativeCandidateValidationIssue::IdentityRead;
        Invalidate();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    try
    {
        if (restore_undo_scratch_ == nullptr)
            restore_undo_scratch_ = std::make_unique<NativeCandidateImage>();
        if (restore_verification_scratch_ == nullptr)
            restore_verification_scratch_ =
                std::make_unique<NativeCandidateImage>();
    }
    catch (...)
    {
        Invalidate();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    bound_ = true;
    NativeCandidateImage ignored{};
    if (!capture_unchecked(ignored))
    {
        Invalidate();
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    return Status::success();
}

void NativeCandidateRegions::Invalidate() noexcept
{
    addresses_ = {};
    identities_ = {};
    bound_ = false;
}

Status NativeCandidateRegions::PreflightCapture() noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    if (identities_match()) return Status::success();
    validation_diagnostic_.issue = NativeCandidateValidationIssue::IdentityRead;
    return Status::failure(FailureCode::IdentityMismatch);
}

std::size_t NativeCandidateRegions::ScratchCapacityBytes() const noexcept
{
    const auto capacity = [](const auto& image) noexcept {
        return image == nullptr ? std::size_t{} :
            image->stage_wind_emitters.states.capacity()
                * sizeof(std::array<std::byte,
                    native_stage_wind_emitter_state_size>);
    };
    return capacity(restore_undo_scratch_)
        + capacity(restore_verification_scratch_);
}

bool NativeCandidateRegions::capture_unchecked(NativeCandidateImage& output) noexcept
{
    reset_native_candidate(output);
    try
    {
        output.stage_wind_emitters.states.reserve(
            native_stage_wind_emitter_max_count);
    }
    catch (...) { return false; }
    validation_diagnostic_ = {};
    output.session_generation = addresses_.session_generation;
    output.round_generation = addresses_.round_generation;
    const auto region_read_failed = [this](std::uint32_t index) {
        validation_diagnostic_.issue =
            NativeCandidateValidationIssue::CandidateRegionRead;
        validation_diagnostic_.index = index;
        return false;
    };
    if (!capture_input_log_cache(memory_, addresses_.input_log,
            output.input_log, validation_diagnostic_)) return false;
    if (!read_value(memory_, addresses_.frame_counter,
            output.frame.frame_counter)) return region_read_failed(1);
    if (!read_value(memory_, addresses_.input_log + input_log_game_round,
            output.frame.input_game_round)) return region_read_failed(2);
    if (!read_value(memory_, addresses_.input_log + input_log_game_time,
            output.frame.input_game_time)) return region_read_failed(3);
    if (!read_value(memory_, addresses_.battle_manager + manager_game_round_cursor,
            output.frame.manager_game_round_cursor)) return region_read_failed(4);
    if (!read_value(memory_, addresses_.battle_manager + manager_game_time_cursor,
            output.frame.manager_game_time_cursor)) return region_read_failed(5);
    if (!read_value(memory_, addresses_.battle_manager + manager_round_state_frame,
            output.frame.round_state_frame)) return region_read_failed(6);
    if (!read_value(memory_, addresses_.battle_manager + manager_unpause_countdown,
            output.frame.unpause_countdown)) return region_read_failed(7);
    if (!read_value(memory_, addresses_.battle_manager + manager_repeat_pending,
            output.frame.repeat_pending)) return region_read_failed(8);
    if (!read_value(memory_, addresses_.battle_manager + manager_pending_move_state,
            output.frame.pending_move_state)) return region_read_failed(9);
    if (!read_value(memory_, addresses_.battle_manager + manager_pending_dispatch,
            output.frame.pending_dispatch)) return region_read_failed(10);
    if (!read_value(memory_, addresses_.battle_manager + manager_round_image_applied,
            output.frame.round_image_applied)) return region_read_failed(11);
    if (!read_bytes(identities_.previous_input_array,
            std::as_writable_bytes(std::span{output.frame.previous_inputs})))
        return region_read_failed(12);
    if (!read_bytes(identities_.input_pair_array,
            std::as_writable_bytes(std::span{output.frame.input_pairs})))
        return region_read_failed(13);
    if (!read_bytes(identities_.prior_input_pair_array,
            std::as_writable_bytes(std::span{output.frame.prior_input_pairs})))
        return region_read_failed(14);
    if (!validate_input_log_image(
            output.input_log, output.frame, validation_diagnostic_)) return false;
    std::int32_t round_sequence_count{};
    if (!read_value(memory_, addresses_.battle_manager
            + manager_round_sequence_count, round_sequence_count)
        || round_sequence_count < 0
        || round_sequence_count
            > static_cast<std::int32_t>(native_round_sequence_max_states)
        || round_sequence_count > identities_.round_sequence_capacity
        || !read_value(memory_, addresses_.battle_manager
            + manager_round_sequence_state,
            output.round_sequence.current_state))
    {
        return region_read_failed(16);
    }
    output.round_sequence.count = static_cast<std::uint8_t>(round_sequence_count);
    if (round_sequence_count != 0
        && !read_bytes(identities_.round_sequence_array,
            std::as_writable_bytes(std::span{output.round_sequence.states})
                .first(static_cast<std::size_t>(round_sequence_count))))
    {
        return region_read_failed(17);
    }
    if (!read_bytes(
            identities_.event_mask_owner,
            std::as_writable_bytes(std::span{output.move_dispatch_masks})))
    {
        return false;
    }
    for (std::size_t fighter = 0;
         fighter < output.vfx_edges.fighters.size(); ++fighter)
    {
        for (std::size_t field = 0;
             field < vfx_edge_diagnostic_offsets.size(); ++field)
        {
            if (!read_value(memory_, addresses_.fighter_roots[fighter]
                    + vfx_edge_diagnostic_offsets[field],
                output.vfx_edges.fighters[fighter][field]))
            {
                return region_read_failed(static_cast<std::uint32_t>(50
                    + fighter * vfx_edge_diagnostic_offsets.size() + field));
            }
        }
    }
    for (std::size_t fighter = 0;
         fighter < output.movevm_state_shorts.fighters.size(); ++fighter)
    {
        if (!read_bytes(addresses_.fighter_roots[fighter]
                + movevm_state_shorts_offset,
            std::as_writable_bytes(std::span{
                output.movevm_state_shorts.fighters[fighter]})))
        {
            return region_read_failed(static_cast<std::uint32_t>(58 + fighter));
        }
    }
    if (!read_bytes(addresses_.pump_state + 0x20, output.pump.lane_a)
        || !read_bytes(addresses_.pump_state + 0x50, output.pump.lane_b)
        || !read_bytes(addresses_.pump_state + 0x70, output.pump.controls))
    {
        return false;
    }
    std::int32_t state{};
    std::uint32_t enabled{};
    std::memcpy(&state, output.pump.controls.data(), sizeof(state));
    std::memcpy(&enabled, output.pump.controls.data() + 0x0C, sizeof(enabled));
    if (state < 0 || state > 4 || enabled > 1) return false;
    for (std::size_t lane = 0; lane < output.schedulers.size(); ++lane)
    {
        const auto scheduler = addresses_.scheduler_base + lane * 0x60;
        auto& image = output.schedulers[lane];
        if (!read_bytes(scheduler + 0x08, image.published_input)
            || !read_bytes(scheduler + 0x30, image.command_state)
            || !read_bytes(scheduler + 0x58, image.active_slot))
        {
            return false;
        }
        std::uint32_t active_slot{};
        std::memcpy(&active_slot, image.active_slot.data(), sizeof(active_slot));
        if (active_slot > 1) return false;
    }
    for (std::size_t lane = 0; lane < output.sub_vms.size(); ++lane)
    {
        auto& image = output.sub_vms[lane];
        const auto& identity = identities_.sub_vms[lane];
        image.vtable_rva = static_cast<std::uint32_t>(
            identity.vtable - addresses_.image_base);
        image.extent = identity.extent;
        if (!read_bytes(identity.object + 0x08, image.input_command)
            || !read_bytes(identity.object + 0x20, image.common))
        {
            return false;
        }
        const auto bytes = derived_size(identity.extent);
        if (bytes != 0
            && !read_bytes(identity.object + 0x68, std::span{image.derived}.first(bytes)))
        {
            return false;
        }
    }
    for (std::size_t lane = 0; lane < output.move_commands.size(); ++lane)
    {
        const auto slot = addresses_.move_command_base + lane * 0x3038;
        std::size_t destination = 0;
        for (const auto range : move_command_ranges)
        {
            auto bank = std::span{output.move_commands[lane]}.subspan(
                destination, range.size);
            if (!read_bytes(slot + range.offset, bank)) return false;
            destination += range.size;
        }
        if (!read_bytes(
                addresses_.slot_param_base + lane * 0x2C,
                output.slot_params[lane]))
        {
            return false;
        }
    }
    std::uintptr_t pending_attacker{};
    if (!read_value(memory_, addresses_.pending_hit_record,
            output.pending_hit.reaction_move_id)
        || !read_value(memory_, addresses_.pending_hit_record + 4,
            output.pending_hit.launcher_facing_delta)
        || !read_value(memory_, addresses_.pending_hit_record + 8,
            pending_attacker)
        || !read_value(memory_, addresses_.pending_hit_record + 0x10,
            output.pending_hit.transition_flags)
        || !read_value(memory_, addresses_.pending_launcher_sync,
            output.pending_hit.launcher_sync))
    {
        return region_read_failed(13);
    }
    if (pending_attacker == 0)
        output.pending_hit.attacker_slot = 0;
    else if (pending_attacker == addresses_.fighter_roots[0])
        output.pending_hit.attacker_slot = 1;
    else if (pending_attacker == addresses_.fighter_roots[1])
        output.pending_hit.attacker_slot = 2;
    else
        return region_read_failed(14);
    if (!valid_pending_hit(output.pending_hit)) return region_read_failed(15);
    if (!read_value(memory_, addresses_.lcg_rng, output.rng.lcg)
        || !read_bytes(addresses_.lfsr_rng,
            std::as_writable_bytes(std::span{output.rng.lfsr}))
        || !read_value(memory_, addresses_.lfsr_rng + 0x64,
            output.rng.lfsr_index)
        || !read_bytes(addresses_.xorshift_rng,
            std::as_writable_bytes(std::span{output.rng.xorshift}))
        || !read_bytes(addresses_.wind_rng,
            std::as_writable_bytes(std::span{output.rng.wind}))
        || output.rng.lfsr_index > output.rng.lfsr.size())
    {
        return false;
    }
    if (!read_bytes(addresses_.vm_freeze_record, output.vm_freeze_record))
        return region_read_failed(18);
    try
    {
        output.stage_wind_emitters.states.resize(
            identities_.stage_wind_emitter_count);
    }
    catch (...) { return region_read_failed(19); }
    for (std::size_t index = 0;
         index < output.stage_wind_emitters.states.size(); ++index)
    {
        if (!read_bytes(identities_.stage_wind_emitters[index],
                output.stage_wind_emitters.states[index]))
            return region_read_failed(static_cast<std::uint32_t>(19 + index));
    }
    for (std::size_t index = 0;
         index < output.camera_components.size(); ++index)
    {
        const auto& identity = identities_.camera_components[index];
        auto& component = output.camera_components[index];
        if (identity.object == 0) continue;
        component.present = 1;
        component.serialization = identity.serialization;
        component.vtable_rva = static_cast<std::uint32_t>(
            identity.vtable - addresses_.image_base);
        component.writer_rva = static_cast<std::uint32_t>(
            identity.writer - addresses_.image_base);
        component.derived_size = camera_derived_size(identity.serialization);
        std::size_t cursor{};
        for (const auto range : camera_component_common_ranges)
        {
            if (!read_bytes(identity.object + range.offset,
                    std::span{component.common}.subspan(cursor, range.size)))
                return region_read_failed(static_cast<std::uint32_t>(70 + index));
            cursor += range.size;
        }
        switch (identity.serialization)
        {
        case NativeCameraComponentSerialization::StateBuffer:
            if (!read_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x140)))
                return region_read_failed(static_cast<std::uint32_t>(90 + index));
            break;
        case NativeCameraComponentSerialization::State:
            if (!read_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x1C)))
                return region_read_failed(static_cast<std::uint32_t>(90 + index));
            break;
        case NativeCameraComponentSerialization::CharaReference:
        {
            std::uintptr_t tracked{};
            if (!read_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x14))
                || !read_value(memory_, identity.object + 0x1F0, tracked))
                return region_read_failed(static_cast<std::uint32_t>(90 + index));
            component.tracked_chara_slot = tracked == 0 ? -1
                : tracked == addresses_.fighter_roots[0] ? 0
                : tracked == addresses_.fighter_roots[1] ? 1 : -2;
            if (component.tracked_chara_slot == -2)
                return region_read_failed(static_cast<std::uint32_t>(110 + index));
            break;
        }
        case NativeCameraComponentSerialization::Attention:
            if (!read_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x0C))
                || !read_bytes(identity.object + 0x1E8,
                    std::span{component.derived}.subspan(0x0C, 0x04)))
                return region_read_failed(static_cast<std::uint32_t>(90 + index));
            break;
        case NativeCameraComponentSerialization::Stay:
            if (!read_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x0C)))
                return region_read_failed(static_cast<std::uint32_t>(90 + index));
            break;
        case NativeCameraComponentSerialization::PlayerWatchActive:
        {
            std::size_t derived_cursor{};
            for (const auto range : player_watch_active_ranges)
            {
                if (!read_bytes(identity.object + range.offset,
                        std::span{component.derived}.subspan(
                            derived_cursor, range.size)))
                    return region_read_failed(static_cast<std::uint32_t>(90 + index));
                derived_cursor += range.size;
            }
            break;
        }
        case NativeCameraComponentSerialization::Base:
            break;
        default:
            return region_read_failed(static_cast<std::uint32_t>(90 + index));
        }
        if (!valid_camera_component(component))
            return region_read_failed(static_cast<std::uint32_t>(110 + index));
    }
    for (std::size_t index = 0;
         index < output.camera_distance_history.size(); ++index)
    {
        auto& history = output.camera_distance_history[index];
        const auto vtable = identities_.camera_vtables[index];
        if (vtable != addresses_.image_base + player_watch_camera_vtable_rva)
            continue;
        const auto action = addresses_.camera_action_backing
            + index * camera_action_stride;
        std::uintptr_t current_vtable{};
        history.present = 1;
        if (!read_value(memory_, action, current_vtable)
            || current_vtable != vtable
            || !read_bytes(action + camera_distance_history_offset,
                std::as_writable_bytes(std::span{history.sample_bits}))
            || !read_value(memory_, action + 0x29C, history.sample_count)
            || !read_value(memory_, action + 0x2A0, history.cursor)
            || !valid_camera_history(history))
        {
            return region_read_failed(static_cast<std::uint32_t>(30 + index));
        }
    }
    return true;
}

Status NativeCandidateRegions::Capture(NativeCandidateImage& output) noexcept
{
    const auto preflight = PreflightCapture();
    if (!preflight.ok()) return preflight;
    return capture_unchecked(output)
        ? Status::success()
        : Status::failure(FailureCode::CaptureFailed);
}

bool NativeCandidateRegions::capture_camera_component(
    std::size_t index, NativeCameraComponentImage& output) noexcept
{
    output = {};
    if (index >= identities_.camera_components.size()) return false;
    const auto& identity = identities_.camera_components[index];
    if (identity.object == 0) return true;
    std::uintptr_t current_vtable{};
    if (!read_value(memory_, identity.object, current_vtable)
        || current_vtable != identity.vtable)
    {
        return false;
    }
    output.present = 1;
    output.serialization = identity.serialization;
    output.vtable_rva = static_cast<std::uint32_t>(
        identity.vtable - addresses_.image_base);
    output.writer_rva = static_cast<std::uint32_t>(
        identity.writer - addresses_.image_base);
    output.derived_size = camera_derived_size(identity.serialization);
    std::size_t cursor{};
    for (const auto range : camera_component_common_ranges)
    {
        if (!read_bytes(identity.object + range.offset,
                std::span{output.common}.subspan(cursor, range.size)))
            return false;
        cursor += range.size;
    }
    switch (identity.serialization)
    {
    case NativeCameraComponentSerialization::StateBuffer:
        if (!read_bytes(identity.object + 0x1D0,
                std::span{output.derived}.first(0x140))) return false;
        break;
    case NativeCameraComponentSerialization::State:
        if (!read_bytes(identity.object + 0x1D0,
                std::span{output.derived}.first(0x1C))) return false;
        break;
    case NativeCameraComponentSerialization::CharaReference:
    {
        std::uintptr_t tracked{};
        if (!read_bytes(identity.object + 0x1D0,
                std::span{output.derived}.first(0x14))
            || !read_value(memory_, identity.object + 0x1F0, tracked))
            return false;
        output.tracked_chara_slot = tracked == 0 ? -1
            : tracked == addresses_.fighter_roots[0] ? 0
            : tracked == addresses_.fighter_roots[1] ? 1 : -2;
        if (output.tracked_chara_slot == -2) return false;
        break;
    }
    case NativeCameraComponentSerialization::Attention:
        if (!read_bytes(identity.object + 0x1D0,
                std::span{output.derived}.first(0x0C))
            || !read_bytes(identity.object + 0x1E8,
                std::span{output.derived}.subspan(0x0C, 0x04))) return false;
        break;
    case NativeCameraComponentSerialization::Stay:
        if (!read_bytes(identity.object + 0x1D0,
                std::span{output.derived}.first(0x0C))) return false;
        break;
    case NativeCameraComponentSerialization::PlayerWatchActive:
        cursor = 0;
        for (const auto range : player_watch_active_ranges)
        {
            if (!read_bytes(identity.object + range.offset,
                    std::span{output.derived}.subspan(cursor, range.size)))
                return false;
            cursor += range.size;
        }
        break;
    case NativeCameraComponentSerialization::Base:
        break;
    default:
        return false;
    }
    return valid_camera_component(output);
}

bool NativeCandidateRegions::write_camera_component(
    std::size_t index, const NativeCameraComponentImage& image,
    bool reverse) noexcept
{
    if (index >= identities_.camera_components.size()
        || image.present == 0
        || !valid_camera_component(image))
    {
        return false;
    }
    const auto& identity = identities_.camera_components[index];
    if (identity.object == 0
        || identity.serialization != image.serialization
        || image.vtable_rva != identity.vtable - addresses_.image_base
        || image.writer_rva != identity.writer - addresses_.image_base)
    {
        return false;
    }
    std::uintptr_t current_vtable{};
    if (!read_value(memory_, identity.object, current_vtable)
        || current_vtable != identity.vtable)
    {
        return false;
    }
    std::array<std::size_t, camera_component_common_ranges.size()>
        common_starts{};
    std::array<std::size_t, player_watch_active_ranges.size()>
        derived_starts{};
    std::size_t cursor{};
    for (std::size_t field = 0;
         field < camera_component_common_ranges.size(); ++field)
    {
        common_starts[field] = cursor;
        cursor += camera_component_common_ranges[field].size;
    }
    cursor = 0;
    for (std::size_t field = 0;
         field < player_watch_active_ranges.size(); ++field)
    {
        derived_starts[field] = cursor;
        cursor += player_watch_active_ranges[field].size;
    }
    const auto write_common_forward = [&]() noexcept
    {
        for (std::size_t field = 0;
             field < camera_component_common_ranges.size(); ++field)
        {
            const auto range = camera_component_common_ranges[field];
            if (!write_bytes(identity.object + range.offset,
                    std::span{image.common}.subspan(
                        common_starts[field], range.size))) return false;
        }
        return true;
    };
    const auto write_common_reverse = [&]() noexcept
    {
        bool ok = true;
        for (std::size_t field = camera_component_common_ranges.size();
             field-- > 0;)
        {
            const auto range = camera_component_common_ranges[field];
            ok = write_bytes(identity.object + range.offset,
                std::span{image.common}.subspan(
                    common_starts[field], range.size)) && ok;
        }
        return ok;
    };
    const auto write_derived = [&](bool reverse_fields) noexcept
    {
        const std::uintptr_t null_pointer{};
        switch (image.serialization)
        {
        case NativeCameraComponentSerialization::StateBuffer:
            return write_bytes(identity.object + 0x1D0,
                std::span{image.derived}.first(0x140));
        case NativeCameraComponentSerialization::State:
            return write_bytes(identity.object + 0x1D0,
                std::span{image.derived}.first(0x1C));
        case NativeCameraComponentSerialization::CharaReference:
        {
            const std::uintptr_t tracked = image.tracked_chara_slot < 0
                ? 0 : addresses_.fighter_roots[static_cast<std::size_t>(
                    image.tracked_chara_slot)];
            if (!reverse_fields)
                return write_bytes(identity.object + 0x1D0,
                        std::span{image.derived}.first(0x14))
                    && write_bytes(identity.object + 0x1E8,
                        std::as_bytes(std::span{&null_pointer, 1}))
                    && write_bytes(identity.object + 0x1F0,
                        std::as_bytes(std::span{&tracked, 1}));
            return write_bytes(identity.object + 0x1F0,
                    std::as_bytes(std::span{&tracked, 1}))
                && write_bytes(identity.object + 0x1E8,
                    std::as_bytes(std::span{&null_pointer, 1}))
                && write_bytes(identity.object + 0x1D0,
                    std::span{image.derived}.first(0x14));
        }
        case NativeCameraComponentSerialization::Attention:
            if (!reverse_fields)
                return write_bytes(identity.object + 0x1D0,
                        std::span{image.derived}.first(0x0C))
                    && write_bytes(identity.object + 0x1E8,
                        std::span{image.derived}.subspan(0x0C, 0x04))
                    && write_bytes(identity.object + 0x1E0,
                        std::as_bytes(std::span{&null_pointer, 1}));
            return write_bytes(identity.object + 0x1E0,
                    std::as_bytes(std::span{&null_pointer, 1}))
                && write_bytes(identity.object + 0x1E8,
                    std::span{image.derived}.subspan(0x0C, 0x04))
                && write_bytes(identity.object + 0x1D0,
                    std::span{image.derived}.first(0x0C));
        case NativeCameraComponentSerialization::Stay:
            if (!reverse_fields)
                return write_bytes(identity.object + 0x1D0,
                        std::span{image.derived}.first(0x0C))
                    && write_bytes(identity.object + 0x1E0,
                        std::as_bytes(std::span{&null_pointer, 1}));
            return write_bytes(identity.object + 0x1E0,
                    std::as_bytes(std::span{&null_pointer, 1}))
                && write_bytes(identity.object + 0x1D0,
                    std::span{image.derived}.first(0x0C));
        case NativeCameraComponentSerialization::PlayerWatchActive:
            if (!reverse_fields)
            {
                for (std::size_t field = 0;
                     field < player_watch_active_ranges.size(); ++field)
                {
                    const auto range = player_watch_active_ranges[field];
                    if (!write_bytes(identity.object + range.offset,
                            std::span{image.derived}.subspan(
                                derived_starts[field], range.size))) return false;
                }
                return true;
            }
            else
            {
                bool ok = true;
                for (std::size_t field = player_watch_active_ranges.size();
                     field-- > 0;)
                {
                    const auto range = player_watch_active_ranges[field];
                    ok = write_bytes(identity.object + range.offset,
                        std::span{image.derived}.subspan(
                            derived_starts[field], range.size)) && ok;
                }
                return ok;
            }
        case NativeCameraComponentSerialization::Base:
            return true;
        default:
            return false;
        }
    };
    return !reverse
        ? write_common_forward() && write_derived(false)
        : write_derived(true) && write_common_reverse();
}

Status NativeCandidateRegions::CaptureCameraSourceFrame(
    NativeCameraSourceFrameImage& output) noexcept
{
    output = {};
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    output.session_generation = addresses_.session_generation;
    output.round_generation = addresses_.round_generation;
    for (std::size_t index = 0;
         index < identities_.camera_components.size(); ++index)
    {
        if (!capture_camera_component(index, output.components[index]))
            return Status::failure(FailureCode::CaptureFailed);
    }
    for (std::size_t fighter = 0;
         fighter < output.fighter_render_positions.size(); ++fighter)
    {
        if (!read_bytes(addresses_.fighter_roots[fighter]
                + camera_fighter_render_position_offset,
            output.fighter_render_positions[fighter]))
            return Status::failure(FailureCode::CaptureFailed);
    }
    if (!read_bytes(addresses_.camera_director, output.director_state)
        || !read_bytes(addresses_.camera_velocity_basis,
            output.velocity_basis)
        || !read_bytes(addresses_.camera_timer_config + 0xA8,
            output.timer_config_state)
        || !read_bytes(addresses_.camera_timer_node, output.timer_node)
        || !read_bytes(addresses_.camera_action_backing,
            output.action_backing))
        return Status::failure(FailureCode::CaptureFailed);
    for (std::size_t index = 0; index < output.timer_globals.size(); ++index)
    {
        if (!read_value(memory_, addresses_.camera_timer_globals[index],
                output.timer_globals[index]))
            return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}

Status NativeCandidateRegions::RestoreCameraSourceFrameTransactional(
    const NativeCameraSourceFrameImage& image) noexcept
{
    if (!bound_ || image.session_generation != addresses_.session_generation
        || image.round_generation != addresses_.round_generation)
    {
        return Status::failure(FailureCode::GenerationMismatch);
    }
    for (std::size_t index = 0; index < image.components.size(); ++index)
    {
        const auto& identity = identities_.camera_components[index];
        const auto& component = image.components[index];
        if ((component.present != 0) != (identity.object != 0)
            || !valid_camera_component(component)
            || (component.present != 0
                && (component.serialization != identity.serialization
                    || component.vtable_rva
                        != identity.vtable - addresses_.image_base
                    || component.writer_rva
                        != identity.writer - addresses_.image_base)))
        {
            return Status::failure(FailureCode::IdentityMismatch);
        }
    }
    NativeCameraSourceFrameImage current{};
    const Status current_status = CaptureCameraSourceFrame(current);
    if (!current_status.ok()) return current_status;
    const auto read_pointer = [](const auto& bytes, std::size_t offset) noexcept {
        std::uintptr_t value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    const auto read_u32 = [](const auto& bytes, std::size_t offset) noexcept {
        std::uint32_t value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    const auto current_owner = read_pointer(current.timer_node, 0);
    if (current_owner == 0
        || read_pointer(image.director_state, 0)
            != read_pointer(current.director_state, 0)
        || read_pointer(image.director_state, 0x10)
            != read_pointer(current.director_state, 0x10)
        || read_pointer(current.timer_node, 8)
            != addresses_.camera_action_backing
        || read_pointer(image.timer_node, 0) != current_owner
        || read_pointer(image.timer_node, 8)
            != addresses_.camera_action_backing)
        return Status::failure(FailureCode::IdentityMismatch);
    for (std::size_t index = 0; index < native_camera_component_count; ++index)
    {
        if (read_pointer(image.director_state, 0x270 + index * 8)
                != identities_.camera_components[index].object
            || read_pointer(current.director_state, 0x270 + index * 8)
                != identities_.camera_components[index].object)
            return Status::failure(FailureCode::IdentityMismatch);
    }
    for (std::size_t index = 0; index < native_camera_action_count; ++index)
    {
        const auto action = addresses_.camera_action_backing
            + index * camera_action_stride;
        if (read_pointer(current.timer_node, 0x10 + index * 8) != action
            || read_pointer(image.timer_node, 0x10 + index * 8) != action
            || read_pointer(current.action_backing,
                   index * camera_action_stride) != identities_.camera_vtables[index]
            || read_pointer(image.action_backing,
                   index * camera_action_stride) != identities_.camera_vtables[index]
            || read_u32(image.action_backing,
                   index * camera_action_stride + 0x08) != index
            || read_pointer(image.action_backing,
                   index * camera_action_stride + 0x10) != current_owner
            || read_pointer(image.action_backing,
                   index * camera_action_stride + 0x18)
                != addresses_.camera_timer_node)
            return Status::failure(FailureCode::IdentityMismatch);
    }
    NativeCameraSourceFrameImage undo{};
    undo = current;
    const auto write_source = [&](const NativeCameraSourceFrameImage& source,
                                  bool reverse) noexcept {
        // Match the native HgCpu reader: director first, then typed
        // components, timer node/backing, child state, and trailing globals.
        bool ok = write_bytes(addresses_.camera_director,
            source.director_state);
        if (!ok && !reverse) return false;
        // Camera actions and the gated synthesis velocity pass consume this
        // matrix, so restore it before either component/action state can run.
        ok = write_bytes(addresses_.camera_velocity_basis,
            source.velocity_basis) && ok;
        if (!ok && !reverse) return false;
        for (std::size_t index = 0; index < source.components.size(); ++index)
        {
            if (source.components[index].present == 0) continue;
            ok = write_camera_component(
                index, source.components[index], reverse) && ok;
            if (!ok && !reverse) return false;
        }
        // Restore the render-cache values before the next camera synthesis.
        // They are regenerated from canonical root-step state during each
        // native frame but otherwise survive a rollback load at the later
        // authoritative frame.
        for (std::size_t fighter = 0;
             fighter < source.fighter_render_positions.size(); ++fighter)
        {
            ok = write_bytes(addresses_.fighter_roots[fighter]
                    + camera_fighter_render_position_offset,
                source.fighter_render_positions[fighter]) && ok;
            if (!ok && !reverse) return false;
        }
        const std::array<std::byte, 16> live_node_identity = [&] {
            std::array<std::byte, 16> value{};
            std::memcpy(value.data(), current.timer_node.data(), value.size());
            return value;
        }();
        ok = write_bytes(addresses_.camera_timer_node, source.timer_node) && ok;
        ok = write_bytes(addresses_.camera_timer_node, live_node_identity) && ok;
        ok = write_bytes(addresses_.camera_action_backing,
            source.action_backing) && ok;
        // Match the native reader's post-node fixed-tail restore and include
        // the verified +0x138/+0x13C selector/producer inputs omitted by the
        // generic HgCpu archive.
        ok = write_bytes(addresses_.camera_timer_config + 0xA8,
            source.timer_config_state) && ok;
        for (std::size_t index = 0; index < source.timer_globals.size(); ++index)
        {
            ok = write_bytes(addresses_.camera_timer_globals[index],
                std::as_bytes(std::span{source.timer_globals.data() + index, 1}))
                && ok;
        }
        return ok;
    };
    const bool wrote = write_source(image, false);
    NativeCameraSourceFrameImage verification{};
    if (wrote && CaptureCameraSourceFrame(verification).ok()
        && verification == image)
    {
        return Status::success();
    }
    const bool undone = write_source(undo, true);
    if (!undone) return Status::failure(FailureCode::UndoFailed);
    NativeCameraSourceFrameImage undo_verification{};
    if (!CaptureCameraSourceFrame(undo_verification).ok()
        || undo_verification != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(wrote
        ? FailureCode::RestoreVerificationFailed
        : FailureCode::RestoreWriteFailed);
}

bool NativeCandidateRegions::image_matches_binding(
    const NativeCandidateImage& image) const noexcept
{
    if (image.session_generation != addresses_.session_generation
        || image.round_generation != addresses_.round_generation)
    {
        return false;
    }
    NativeCandidateValidationDiagnostic diagnostic{};
    if (!validate_input_log_image(image.input_log, image.frame, diagnostic)) return false;
    for (std::size_t lane = 0; lane < image.sub_vms.size(); ++lane)
    {
        const auto& identity = identities_.sub_vms[lane];
        const auto& state = image.sub_vms[lane];
        if (state.extent != identity.extent
            || state.vtable_rva != identity.vtable - addresses_.image_base
            || derived_size(state.extent) > state.derived.size())
        {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < image.camera_components.size(); ++index)
    {
        const auto& identity = identities_.camera_components[index];
        const auto& component = image.camera_components[index];
        if (!valid_camera_component(component)
            || (component.present != 0
                && (component.serialization != identity.serialization
                    || component.vtable_rva
                        != identity.vtable - addresses_.image_base
                    || component.writer_rva
                        != identity.writer - addresses_.image_base)))
            return false;
    }
    for (std::size_t index = 0;
         index < image.camera_distance_history.size(); ++index)
    {
        const bool expected = identities_.camera_vtables[index]
            == addresses_.image_base + player_watch_camera_vtable_rva;
        if (!valid_camera_history(image.camera_distance_history[index])
            || (image.camera_distance_history[index].present != 0) != expected)
        {
            return false;
        }
    }
    return valid_pending_hit(image.pending_hit)
        && image.round_sequence.count <= native_round_sequence_max_states
        && image.round_sequence.count <= identities_.round_sequence_capacity
        && image.rng.lfsr_index <= image.rng.lfsr.size()
        && image.stage_wind_emitters.states.size()
            == identities_.stage_wind_emitter_count;
}

Status NativeCandidateRegions::PreflightRestore(
    const NativeCandidateImage& image) noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    if (!image_matches_binding(image))
        return Status::failure(FailureCode::GenerationMismatch);
    return identities_match()
        ? Status::success()
        : Status::failure(FailureCode::IdentityMismatch);
}

bool NativeCandidateRegions::write_forward(const NativeCandidateImage& image) noexcept
{
    const std::uintptr_t pending_attacker = image.pending_hit.attacker_slot == 0
        ? 0 : addresses_.fighter_roots[image.pending_hit.attacker_slot - 1];
    const std::int32_t round_sequence_count = image.round_sequence.count;
    if (!write_input_log_cache(memory_, addresses_.input_log, image.input_log)
        || !write_bytes(identities_.previous_input_array,
            std::as_bytes(std::span{image.frame.previous_inputs}))
        || !write_bytes(identities_.input_pair_array,
            std::as_bytes(std::span{image.frame.input_pairs}))
        || !write_bytes(identities_.prior_input_pair_array,
            std::as_bytes(std::span{image.frame.prior_input_pairs}))
        || !write_bytes(addresses_.input_log + input_log_game_round,
            std::as_bytes(std::span{&image.frame.input_game_round, 1}))
        || !write_bytes(addresses_.input_log + input_log_game_time,
            std::as_bytes(std::span{&image.frame.input_game_time, 1}))
        || !write_bytes(addresses_.battle_manager + manager_game_round_cursor,
            std::as_bytes(std::span{&image.frame.manager_game_round_cursor, 1}))
        || !write_bytes(addresses_.battle_manager + manager_game_time_cursor,
            std::as_bytes(std::span{&image.frame.manager_game_time_cursor, 1}))
        || !write_bytes(addresses_.battle_manager + manager_round_state_frame,
            std::as_bytes(std::span{&image.frame.round_state_frame, 1}))
        || !write_bytes(addresses_.battle_manager + manager_unpause_countdown,
            std::as_bytes(std::span{&image.frame.unpause_countdown, 1}))
        || !write_bytes(addresses_.battle_manager + manager_repeat_pending,
            std::as_bytes(std::span{&image.frame.repeat_pending, 1}))
        || !write_bytes(addresses_.battle_manager + manager_pending_move_state,
            std::as_bytes(std::span{&image.frame.pending_move_state, 1}))
        || !write_bytes(addresses_.battle_manager + manager_pending_dispatch,
            std::as_bytes(std::span{&image.frame.pending_dispatch, 1}))
        || !write_bytes(addresses_.battle_manager + manager_round_image_applied,
            std::as_bytes(std::span{&image.frame.round_image_applied, 1}))
        || (round_sequence_count != 0
            && !write_bytes(identities_.round_sequence_array,
                std::as_bytes(std::span{image.round_sequence.states})
                    .first(static_cast<std::size_t>(round_sequence_count))))
        || !write_bytes(addresses_.battle_manager + manager_round_sequence_count,
            std::as_bytes(std::span{&round_sequence_count, 1}))
        || !write_bytes(addresses_.battle_manager + manager_round_sequence_state,
            std::as_bytes(std::span{&image.round_sequence.current_state, 1}))
        || !write_bytes(addresses_.frame_counter,
            std::as_bytes(std::span{&image.frame.frame_counter, 1}))
        || !write_bytes(addresses_.pending_hit_record,
            std::as_bytes(std::span{&image.pending_hit.reaction_move_id, 1}))
        || !write_bytes(addresses_.pending_hit_record + 4,
            std::as_bytes(std::span{&image.pending_hit.launcher_facing_delta, 1}))
        || !write_bytes(addresses_.pending_hit_record + 8,
            std::as_bytes(std::span{&pending_attacker, 1}))
        || !write_bytes(addresses_.pending_hit_record + 0x10,
            std::as_bytes(std::span{&image.pending_hit.transition_flags, 1}))
        || !write_bytes(addresses_.pending_launcher_sync,
            std::as_bytes(std::span{&image.pending_hit.launcher_sync, 1}))
        || !write_bytes(addresses_.wind_rng,
            std::as_bytes(std::span{image.rng.wind}))
        || !write_bytes(addresses_.xorshift_rng,
            std::as_bytes(std::span{image.rng.xorshift}))
        || !write_bytes(addresses_.lfsr_rng,
            std::as_bytes(std::span{image.rng.lfsr}))
        || !write_bytes(addresses_.lfsr_rng + 0x64,
            std::as_bytes(std::span{&image.rng.lfsr_index, 1}))
        || !write_bytes(addresses_.lcg_rng,
            std::as_bytes(std::span{&image.rng.lcg, 1}))
        || !write_bytes(addresses_.vm_freeze_record,
            image.vm_freeze_record))
    {
        return false;
    }
    for (std::size_t index = 0;
         index < image.stage_wind_emitters.states.size(); ++index)
    {
        if (!write_bytes(identities_.stage_wind_emitters[index],
                image.stage_wind_emitters.states[index])) return false;
    }
    if (!write_bytes(
            identities_.event_mask_owner,
            std::as_bytes(std::span{image.move_dispatch_masks}))) return false;
    if (!write_bytes(addresses_.pump_state + 0x20, image.pump.lane_a)
        || !write_bytes(addresses_.pump_state + 0x50, image.pump.lane_b)
        || !write_bytes(addresses_.pump_state + 0x70, image.pump.controls)) return false;
    for (std::size_t lane = 0; lane < image.schedulers.size(); ++lane)
    {
        const auto scheduler = addresses_.scheduler_base + lane * 0x60;
        if (!write_bytes(scheduler + 0x08, image.schedulers[lane].published_input)
            || !write_bytes(scheduler + 0x30, image.schedulers[lane].command_state)
            || !write_bytes(scheduler + 0x58, image.schedulers[lane].active_slot)) return false;
    }
    for (std::size_t lane = 0; lane < image.sub_vms.size(); ++lane)
    {
        const auto object = identities_.sub_vms[lane].object;
        if (!write_bytes(object + 0x08, image.sub_vms[lane].input_command)
            || !write_bytes(object + 0x20, image.sub_vms[lane].common)) return false;
        const auto bytes = derived_size(image.sub_vms[lane].extent);
        if (bytes != 0
            && !write_bytes(object + 0x68, std::span{image.sub_vms[lane].derived}.first(bytes)))
            return false;
    }
    for (std::size_t lane = 0; lane < image.move_commands.size(); ++lane)
    {
        const auto slot = addresses_.move_command_base + lane * 0x3038;
        std::size_t source = 0;
        for (const auto range : move_command_ranges)
        {
            const auto bank = std::span{image.move_commands[lane]}.subspan(source, range.size);
            if (!write_bytes(slot + range.offset, bank)) return false;
            source += range.size;
        }
    }
    for (std::size_t lane = 0; lane < image.slot_params.size(); ++lane)
        if (!write_bytes(addresses_.slot_param_base + lane * 0x2C, image.slot_params[lane]))
            return false;
    for (std::size_t index = 0;
         index < image.camera_components.size(); ++index)
    {
        const auto& component = image.camera_components[index];
        const auto& identity = identities_.camera_components[index];
        if (component.present == 0) continue;
        std::size_t cursor{};
        for (const auto range : camera_component_common_ranges)
        {
            if (!write_bytes(identity.object + range.offset,
                    std::span{component.common}.subspan(cursor, range.size)))
                return false;
            cursor += range.size;
        }
        const std::uintptr_t null_pointer{};
        switch (component.serialization)
        {
        case NativeCameraComponentSerialization::StateBuffer:
            if (!write_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x140))) return false;
            break;
        case NativeCameraComponentSerialization::State:
            if (!write_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x1C))) return false;
            break;
        case NativeCameraComponentSerialization::CharaReference:
        {
            const std::uintptr_t tracked = component.tracked_chara_slot < 0
                ? 0 : addresses_.fighter_roots[static_cast<std::size_t>(
                    component.tracked_chara_slot)];
            if (!write_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x14))
                || !write_bytes(identity.object + 0x1E8,
                    std::as_bytes(std::span{&null_pointer, 1}))
                || !write_bytes(identity.object + 0x1F0,
                    std::as_bytes(std::span{&tracked, 1}))) return false;
            break;
        }
        case NativeCameraComponentSerialization::Attention:
            if (!write_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x0C))
                || !write_bytes(identity.object + 0x1E8,
                    std::span{component.derived}.subspan(0x0C, 0x04))
                || !write_bytes(identity.object + 0x1E0,
                    std::as_bytes(std::span{&null_pointer, 1}))) return false;
            break;
        case NativeCameraComponentSerialization::Stay:
            if (!write_bytes(identity.object + 0x1D0,
                    std::span{component.derived}.first(0x0C))
                || !write_bytes(identity.object + 0x1E0,
                    std::as_bytes(std::span{&null_pointer, 1}))) return false;
            break;
        case NativeCameraComponentSerialization::PlayerWatchActive:
        {
            std::size_t derived_cursor{};
            for (const auto range : player_watch_active_ranges)
            {
                if (!write_bytes(identity.object + range.offset,
                        std::span{component.derived}.subspan(
                            derived_cursor, range.size))) return false;
                derived_cursor += range.size;
            }
            break;
        }
        case NativeCameraComponentSerialization::Base:
            break;
        default:
            return false;
        }
    }
    for (std::size_t index = 0;
         index < image.camera_distance_history.size(); ++index)
    {
        const auto& history = image.camera_distance_history[index];
        if (history.present == 0) continue;
        const auto action = addresses_.camera_action_backing
            + index * camera_action_stride;
        std::uintptr_t vtable{};
        if (!read_value(memory_, action, vtable)
            || vtable != identities_.camera_vtables[index]
            || !write_bytes(action + camera_distance_history_offset,
                std::as_bytes(std::span{history.sample_bits}))
            || !write_bytes(action + 0x29C,
                std::as_bytes(std::span{&history.sample_count, 1}))
            || !write_bytes(action + 0x2A0,
                std::as_bytes(std::span{&history.cursor, 1})))
        {
            return false;
        }
    }
    return true;
}

bool NativeCandidateRegions::write_reverse(const NativeCandidateImage& image) noexcept
{
    bool ok = true;
    const std::uintptr_t pending_attacker = image.pending_hit.attacker_slot == 0
        ? 0 : addresses_.fighter_roots[image.pending_hit.attacker_slot - 1];
    const std::int32_t round_sequence_count = image.round_sequence.count;
    for (std::size_t index = image.camera_distance_history.size(); index-- > 0;)
    {
        const auto& history = image.camera_distance_history[index];
        if (history.present == 0) continue;
        const auto action = addresses_.camera_action_backing
            + index * camera_action_stride;
        std::uintptr_t vtable{};
        const bool identity_ok = read_value(memory_, action, vtable)
            && vtable == identities_.camera_vtables[index];
        ok = identity_ok
            && write_bytes(action + 0x2A0,
                std::as_bytes(std::span{&history.cursor, 1})) && ok;
        ok = identity_ok
            && write_bytes(action + 0x29C,
                std::as_bytes(std::span{&history.sample_count, 1})) && ok;
        ok = identity_ok
            && write_bytes(action + camera_distance_history_offset,
                std::as_bytes(std::span{history.sample_bits})) && ok;
    }
    for (std::size_t index = image.camera_components.size(); index-- > 0;)
    {
        const auto& component = image.camera_components[index];
        const auto& identity = identities_.camera_components[index];
        if (component.present == 0) continue;
        const std::uintptr_t null_pointer{};
        switch (component.serialization)
        {
        case NativeCameraComponentSerialization::StateBuffer:
            ok = write_bytes(identity.object + 0x1D0,
                std::span{component.derived}.first(0x140)) && ok;
            break;
        case NativeCameraComponentSerialization::State:
            ok = write_bytes(identity.object + 0x1D0,
                std::span{component.derived}.first(0x1C)) && ok;
            break;
        case NativeCameraComponentSerialization::CharaReference:
        {
            const std::uintptr_t tracked = component.tracked_chara_slot < 0
                ? 0 : addresses_.fighter_roots[static_cast<std::size_t>(
                    component.tracked_chara_slot)];
            ok = write_bytes(identity.object + 0x1F0,
                std::as_bytes(std::span{&tracked, 1})) && ok;
            ok = write_bytes(identity.object + 0x1E8,
                std::as_bytes(std::span{&null_pointer, 1})) && ok;
            ok = write_bytes(identity.object + 0x1D0,
                std::span{component.derived}.first(0x14)) && ok;
            break;
        }
        case NativeCameraComponentSerialization::Attention:
            ok = write_bytes(identity.object + 0x1E0,
                std::as_bytes(std::span{&null_pointer, 1})) && ok;
            ok = write_bytes(identity.object + 0x1E8,
                std::span{component.derived}.subspan(0x0C, 0x04)) && ok;
            ok = write_bytes(identity.object + 0x1D0,
                std::span{component.derived}.first(0x0C)) && ok;
            break;
        case NativeCameraComponentSerialization::Stay:
            ok = write_bytes(identity.object + 0x1E0,
                std::as_bytes(std::span{&null_pointer, 1})) && ok;
            ok = write_bytes(identity.object + 0x1D0,
                std::span{component.derived}.first(0x0C)) && ok;
            break;
        case NativeCameraComponentSerialization::PlayerWatchActive:
        {
            std::array<std::size_t, player_watch_active_ranges.size()> starts{};
            std::size_t cursor{};
            for (std::size_t field = 0;
                 field < player_watch_active_ranges.size(); ++field)
            {
                starts[field] = cursor;
                cursor += player_watch_active_ranges[field].size;
            }
            for (std::size_t field = player_watch_active_ranges.size();
                 field-- > 0;)
            {
                const auto range = player_watch_active_ranges[field];
                ok = write_bytes(identity.object + range.offset,
                    std::span{component.derived}.subspan(
                        starts[field], range.size)) && ok;
            }
            break;
        }
        case NativeCameraComponentSerialization::Base:
            break;
        default:
            ok = false;
            break;
        }
        std::array<std::size_t, camera_component_common_ranges.size()> starts{};
        std::size_t cursor{};
        for (std::size_t field = 0;
             field < camera_component_common_ranges.size(); ++field)
        {
            starts[field] = cursor;
            cursor += camera_component_common_ranges[field].size;
        }
        for (std::size_t field = camera_component_common_ranges.size();
             field-- > 0;)
        {
            const auto range = camera_component_common_ranges[field];
            ok = write_bytes(identity.object + range.offset,
                std::span{component.common}.subspan(
                    starts[field], range.size)) && ok;
        }
    }
    for (std::size_t lane = image.slot_params.size(); lane-- > 0;)
        ok = write_bytes(addresses_.slot_param_base + lane * 0x2C, image.slot_params[lane]) && ok;
    for (std::size_t lane = image.move_commands.size(); lane-- > 0;)
    {
        const auto slot = addresses_.move_command_base + lane * 0x3038;
        std::array<std::size_t, move_command_ranges.size()> starts{};
        std::size_t cursor = 0;
        for (std::size_t i = 0; i < move_command_ranges.size(); ++i)
        {
            starts[i] = cursor;
            cursor += move_command_ranges[i].size;
        }
        for (std::size_t i = move_command_ranges.size(); i-- > 0;)
        {
            const auto range = move_command_ranges[i];
            ok = write_bytes(
                slot + range.offset,
                std::span{image.move_commands[lane]}.subspan(starts[i], range.size)) && ok;
        }
    }
    for (std::size_t lane = image.sub_vms.size(); lane-- > 0;)
    {
        const auto object = identities_.sub_vms[lane].object;
        const auto bytes = derived_size(image.sub_vms[lane].extent);
        if (bytes != 0)
            ok = write_bytes(object + 0x68, std::span{image.sub_vms[lane].derived}.first(bytes)) && ok;
        ok = write_bytes(object + 0x20, image.sub_vms[lane].common) && ok;
        ok = write_bytes(object + 0x08, image.sub_vms[lane].input_command) && ok;
    }
    for (std::size_t lane = image.schedulers.size(); lane-- > 0;)
    {
        const auto scheduler = addresses_.scheduler_base + lane * 0x60;
        ok = write_bytes(scheduler + 0x58, image.schedulers[lane].active_slot) && ok;
        ok = write_bytes(scheduler + 0x30, image.schedulers[lane].command_state) && ok;
        ok = write_bytes(scheduler + 0x08, image.schedulers[lane].published_input) && ok;
    }
    ok = write_bytes(addresses_.pump_state + 0x70, image.pump.controls) && ok;
    ok = write_bytes(addresses_.pump_state + 0x50, image.pump.lane_b) && ok;
    ok = write_bytes(addresses_.pump_state + 0x20, image.pump.lane_a) && ok;
    ok = write_bytes(
        identities_.event_mask_owner,
        std::as_bytes(std::span{image.move_dispatch_masks})) && ok;
    ok = write_bytes(addresses_.lcg_rng,
        std::as_bytes(std::span{&image.rng.lcg, 1})) && ok;
    ok = write_bytes(addresses_.lfsr_rng + 0x64,
        std::as_bytes(std::span{&image.rng.lfsr_index, 1})) && ok;
    ok = write_bytes(addresses_.lfsr_rng,
        std::as_bytes(std::span{image.rng.lfsr})) && ok;
    ok = write_bytes(addresses_.xorshift_rng,
        std::as_bytes(std::span{image.rng.xorshift})) && ok;
    ok = write_bytes(addresses_.wind_rng,
        std::as_bytes(std::span{image.rng.wind})) && ok;
    ok = write_bytes(addresses_.vm_freeze_record,
        image.vm_freeze_record) && ok;
    for (std::size_t index = image.stage_wind_emitters.states.size(); index-- > 0;)
        ok = write_bytes(identities_.stage_wind_emitters[index],
            image.stage_wind_emitters.states[index]) && ok;
    ok = write_bytes(addresses_.pending_launcher_sync,
        std::as_bytes(std::span{&image.pending_hit.launcher_sync, 1})) && ok;
    ok = write_bytes(addresses_.pending_hit_record + 0x10,
        std::as_bytes(std::span{&image.pending_hit.transition_flags, 1})) && ok;
    ok = write_bytes(addresses_.pending_hit_record + 8,
        std::as_bytes(std::span{&pending_attacker, 1})) && ok;
    ok = write_bytes(addresses_.pending_hit_record + 4,
        std::as_bytes(std::span{&image.pending_hit.launcher_facing_delta, 1})) && ok;
    ok = write_bytes(addresses_.pending_hit_record,
        std::as_bytes(std::span{&image.pending_hit.reaction_move_id, 1})) && ok;
    ok = write_bytes(addresses_.frame_counter,
        std::as_bytes(std::span{&image.frame.frame_counter, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_round_sequence_state,
        std::as_bytes(std::span{&image.round_sequence.current_state, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_round_sequence_count,
        std::as_bytes(std::span{&round_sequence_count, 1})) && ok;
    if (round_sequence_count != 0)
        ok = write_bytes(identities_.round_sequence_array,
            std::as_bytes(std::span{image.round_sequence.states})
                .first(static_cast<std::size_t>(round_sequence_count))) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_round_image_applied,
        std::as_bytes(std::span{&image.frame.round_image_applied, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_pending_dispatch,
        std::as_bytes(std::span{&image.frame.pending_dispatch, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_pending_move_state,
        std::as_bytes(std::span{&image.frame.pending_move_state, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_repeat_pending,
        std::as_bytes(std::span{&image.frame.repeat_pending, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_unpause_countdown,
        std::as_bytes(std::span{&image.frame.unpause_countdown, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_round_state_frame,
        std::as_bytes(std::span{&image.frame.round_state_frame, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_game_time_cursor,
        std::as_bytes(std::span{&image.frame.manager_game_time_cursor, 1})) && ok;
    ok = write_bytes(addresses_.battle_manager + manager_game_round_cursor,
        std::as_bytes(std::span{&image.frame.manager_game_round_cursor, 1})) && ok;
    ok = write_bytes(addresses_.input_log + input_log_game_time,
        std::as_bytes(std::span{&image.frame.input_game_time, 1})) && ok;
    ok = write_bytes(addresses_.input_log + input_log_game_round,
        std::as_bytes(std::span{&image.frame.input_game_round, 1})) && ok;
    ok = write_bytes(identities_.prior_input_pair_array,
        std::as_bytes(std::span{image.frame.prior_input_pairs})) && ok;
    ok = write_bytes(identities_.input_pair_array,
        std::as_bytes(std::span{image.frame.input_pairs})) && ok;
    ok = write_bytes(identities_.previous_input_array,
        std::as_bytes(std::span{image.frame.previous_inputs})) && ok;
    ok = write_input_log_cache(memory_, addresses_.input_log, image.input_log) && ok;
    return ok;
}

Status NativeCandidateRegions::RestoreTransactional(
    const NativeCandidateImage& image) noexcept
{
    const auto preflight = PreflightRestore(image);
    if (!preflight.ok()) return preflight;
    if (restore_undo_scratch_ == nullptr
        || restore_verification_scratch_ == nullptr)
        return Status::failure(FailureCode::AdapterUnqualified);
    if (!capture_unchecked(*restore_undo_scratch_))
        return Status::failure(FailureCode::CaptureFailed);
    const bool wrote = write_forward(image);
    const bool captured_verification = wrote
        && capture_unchecked(*restore_verification_scratch_);
    if (captured_verification)
    {
        // Decoded peer checkpoints intentionally omit presentation-local camera
        // component internals.  An absent component means "leave the live local
        // object alone", so exclude only those slots from forward verification.
        // Present component images (including transactional undo captures) retain
        // exact byte-for-byte verification.
        for (std::size_t index = 0;
             index < image.camera_components.size(); ++index)
        {
            if (image.camera_components[index].present == 0)
                restore_verification_scratch_->camera_components[index] =
                    image.camera_components[index];
        }
    }
    const bool verified = captured_verification
        && *restore_verification_scratch_ == image;
    if (verified) return Status::success();
    if (!identities_match() || !write_reverse(*restore_undo_scratch_))
        return Status::failure(FailureCode::UndoFailed);
    if (!capture_unchecked(*restore_verification_scratch_)
        || *restore_verification_scratch_ != *restore_undo_scratch_)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(
        wrote ? FailureCode::RestoreVerificationFailed : FailureCode::RestoreWriteFailed);
}

Status NativeCandidateRegions::RestoreInputLogTransactional(
    const NativeCandidateImage& image) noexcept
{
    validation_diagnostic_ = {};
    NativeCandidateValidationDiagnostic diagnostic{};
    if (!bound_ || !image_matches_binding(image) || !identities_match())
        return Status::failure(FailureCode::IdentityMismatch);
    if (!validate_input_log_image(image.input_log, image.frame, diagnostic))
    {
        validation_diagnostic_ = diagnostic;
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    NativeFrameInputLogImage undo{};
    if (!capture_input_log_cache(
            memory_, addresses_.input_log, undo, validation_diagnostic_))
        return Status::failure(FailureCode::CaptureFailed);
    const bool wrote = write_input_log_cache(
        memory_, addresses_.input_log, image.input_log);
    NativeFrameInputLogImage verified{};
    NativeCandidateValidationDiagnostic verification_diagnostic{};
    if (wrote && capture_input_log_cache(memory_, addresses_.input_log,
            verified, verification_diagnostic) && verified == image.input_log)
        return Status::success();
    const bool undone = write_input_log_cache(
        memory_, addresses_.input_log, undo);
    NativeFrameInputLogImage undo_verified{};
    NativeCandidateValidationDiagnostic undo_diagnostic{};
    if (!undone || !capture_input_log_cache(memory_, addresses_.input_log,
            undo_verified, undo_diagnostic) || undo_verified != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(wrote
        ? FailureCode::RestoreVerificationFailed
        : FailureCode::RestoreWriteFailed);
}

Status NativeCandidateRegions::RestoreMoveDispatchMasksTransactional(
    const NativeCandidateImage& image) noexcept
{
    validation_diagnostic_ = {};
    if (!bound_ || !image_matches_binding(image) || !identities_match()
        || identities_.event_mask_owner == 0)
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    std::array<std::uint64_t, 2> undo{};
    if (!read_bytes(identities_.event_mask_owner,
            std::as_writable_bytes(std::span{undo})))
        return Status::failure(FailureCode::CaptureFailed);
    const bool wrote = write_bytes(identities_.event_mask_owner,
        std::as_bytes(std::span{image.move_dispatch_masks}));
    std::array<std::uint64_t, 2> verified{};
    if (wrote && read_bytes(identities_.event_mask_owner,
            std::as_writable_bytes(std::span{verified}))
        && verified == image.move_dispatch_masks)
    {
        return Status::success();
    }
    const bool undone = write_bytes(identities_.event_mask_owner,
        std::as_bytes(std::span{undo}));
    std::array<std::uint64_t, 2> undo_verified{};
    if (!undone || !read_bytes(identities_.event_mask_owner,
            std::as_writable_bytes(std::span{undo_verified}))
        || undo_verified != undo)
    {
        return Status::failure(FailureCode::UndoFailed);
    }
    return Status::failure(wrote
        ? FailureCode::RestoreVerificationFailed
        : FailureCode::RestoreWriteFailed);
}

Status NativeCandidateRegions::PrepareInputLogTransactional(
    const CanonicalInputDiagnostic& expected,
    const InputPair& input) noexcept
{
    validation_diagnostic_ = {};
    if (!bound_ || !identities_match())
        return Status::failure(FailureCode::IdentityMismatch);
    NativeFrameInputLogImage undo{};
    if (!capture_input_log_cache(
            memory_, addresses_.input_log, undo, validation_diagnostic_))
        return Status::failure(FailureCode::CaptureFailed);
    NativeFrameInputLogImage desired = undo;
    static_assert(sizeof(expected.scalars) == desired.scalars.size());
    std::memcpy(desired.scalars.data(), expected.scalars.data(),
        desired.scalars.size());
    const auto block_begin = expected.scalars[5] & ~0x7fu;
    for (std::size_t slot = 0; slot < 2; ++slot)
        for (std::size_t index = 0; index < 128; ++index)
        {
            const auto frame = block_begin + static_cast<std::uint32_t>(index);
            desired.cache_rows[slot * 512 + (frame & 0x1ffu)] =
                expected.aligned_block_rows[slot * 128 + index];
        }
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        const auto& row = input.source_rows[slot];
        if (row.filled == 0) continue;
        if ((row.frame_index & ~0x7fu) == block_begin) continue;
        desired.cache_rows[slot * 512 + (row.frame_index & 0x1ffu)] = row;
    }
    const bool wrote = write_input_log_cache(
        memory_, addresses_.input_log, desired);
    NativeFrameInputLogImage verified{};
    NativeCandidateValidationDiagnostic verification_diagnostic{};
    if (wrote && capture_input_log_cache(memory_, addresses_.input_log,
            verified, verification_diagnostic) && verified == desired)
        return Status::success();
    const bool undone = write_input_log_cache(
        memory_, addresses_.input_log, undo);
    NativeFrameInputLogImage undo_verified{};
    NativeCandidateValidationDiagnostic undo_diagnostic{};
    if (!undone || !capture_input_log_cache(memory_, addresses_.input_log,
            undo_verified, undo_diagnostic) || undo_verified != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(wrote
        ? FailureCode::RestoreVerificationFailed
        : FailureCode::RestoreWriteFailed);
}

std::vector<std::byte> NativeCandidateRegions::CanonicalBytes(
    const NativeCandidateImage& image)
{
    std::vector<std::byte> output;
    CanonicalBytes(image, output);
    return output;
}

void NativeCandidateRegions::CanonicalBytes(
    const NativeCandidateImage& image, std::vector<std::byte>& output)
{
    output.clear();
    // The two MoveCommand images plus the packed InputLog cache already exceed
    // the old 0x6100 guess. Reserve the measured schema-6 upper bound so normal
    // capture never reallocates while serializing canonical state.
    if (output.capacity() < 0xB000) output.reserve(0xB000);
    append_bytes(output, &image.session_generation, sizeof(image.session_generation));
    append_bytes(output, &image.round_generation, sizeof(image.round_generation));
    append_frame_boundary(output, image.frame);
    append_round_sequence(output, image.round_sequence);
    append_input_log(output, image.input_log);
    append_bytes(output, image.move_dispatch_masks.data(), sizeof(image.move_dispatch_masks));
    append_bytes(output, image.vfx_edges.fighters.data(),
        sizeof(image.vfx_edges.fighters));
    append_bytes(output, image.movevm_state_shorts.fighters.data(),
        sizeof(image.movevm_state_shorts.fighters));
    append_bytes(output, image.pump.lane_a.data(), image.pump.lane_a.size());
    append_bytes(output, image.pump.lane_b.data(), image.pump.lane_b.size());
    append_bytes(output, image.pump.controls.data(), image.pump.controls.size());
    for (const auto& scheduler : image.schedulers)
    {
        append_bytes(output, scheduler.published_input.data(), scheduler.published_input.size());
        append_bytes(output, scheduler.command_state.data(), scheduler.command_state.size());
        append_bytes(output, scheduler.active_slot.data(), scheduler.active_slot.size());
    }
    for (const auto& subvm : image.sub_vms)
    {
        append_bytes(output, &subvm.vtable_rva, sizeof(subvm.vtable_rva));
        append_bytes(output, &subvm.extent, sizeof(subvm.extent));
        append_bytes(output, subvm.input_command.data(), subvm.input_command.size());
        append_bytes(output, subvm.common.data(), subvm.common.size());
        append_bytes(output, subvm.derived.data(), derived_size(subvm.extent));
    }
    for (const auto& command : image.move_commands)
        append_bytes(output, command.data(), command.size());
    for (const auto& param : image.slot_params)
        append_bytes(output, param.data(), param.size());
    append_bytes(output, &image.pending_hit.reaction_move_id,
        sizeof(image.pending_hit.reaction_move_id));
    append_bytes(output, &image.pending_hit.launcher_facing_delta,
        sizeof(image.pending_hit.launcher_facing_delta));
    append_bytes(output, &image.pending_hit.transition_flags,
        sizeof(image.pending_hit.transition_flags));
    append_bytes(output, &image.pending_hit.attacker_slot,
        sizeof(image.pending_hit.attacker_slot));
    append_bytes(output, &image.pending_hit.launcher_sync,
        sizeof(image.pending_hit.launcher_sync));
    append_bytes(output, &image.rng.lcg, sizeof(image.rng.lcg));
    append_bytes(output, image.rng.lfsr.data(), sizeof(image.rng.lfsr));
    append_bytes(output, &image.rng.lfsr_index, sizeof(image.rng.lfsr_index));
    append_bytes(output, image.rng.xorshift.data(), sizeof(image.rng.xorshift));
    append_bytes(output, image.rng.wind.data(), sizeof(image.rng.wind));
    append_bytes(output, image.vm_freeze_record.data(),
        image.vm_freeze_record.size());
    const auto emitter_count = static_cast<std::uint8_t>(
        image.stage_wind_emitters.states.size());
    append_bytes(output, &emitter_count, sizeof(emitter_count));
    for (std::size_t index = 0;
         index < image.stage_wind_emitters.states.size(); ++index)
    {
        append_bytes(output, image.stage_wind_emitters.states[index].data(),
            image.stage_wind_emitters.states[index].size());
    }
    // Camera component internals are local presentation reconstruction state;
    // they deliberately do not enter the peer-canonical byte stream.
    for (const auto& history : image.camera_distance_history)
    {
        append_bytes(output, &history.present, sizeof(history.present));
        if (history.present == 0) continue;
        append_bytes(output, history.sample_bits.data(), sizeof(history.sample_bits));
        append_bytes(output, &history.sample_count, sizeof(history.sample_count));
        append_bytes(output, &history.cursor, sizeof(history.cursor));
    }
}

CanonicalNativeFingerprint NativeCandidateRegions::CanonicalFingerprint(
    const NativeCandidateImage& image)
{
    CanonicalNativeFingerprint output{};
    FingerprintAccumulator bytes;
    const auto finish = [&bytes]() noexcept {
        return bytes.FinishAndReset();
    };
    append_bytes(bytes, &image.frame.frame_counter, sizeof(image.frame.frame_counter));
    output[0] = finish();
    append_bytes(bytes, &image.frame.input_game_round, sizeof(image.frame.input_game_round));
    append_bytes(bytes, &image.frame.input_game_time, sizeof(image.frame.input_game_time));
    output[1] = finish();
    append_bytes(bytes, &image.frame.manager_game_round_cursor, sizeof(image.frame.manager_game_round_cursor));
    append_bytes(bytes, &image.frame.manager_game_time_cursor, sizeof(image.frame.manager_game_time_cursor));
    output[2] = finish();
    append_bytes(bytes, &image.frame.round_state_frame, sizeof(image.frame.round_state_frame));
    append_bytes(bytes, &image.frame.unpause_countdown, sizeof(image.frame.unpause_countdown));
    output[3] = finish();
    append_bytes(bytes, image.frame.previous_inputs.data(), sizeof(image.frame.previous_inputs));
    output[4] = finish();
    append_bytes(bytes, image.frame.input_pairs.data(), sizeof(image.frame.input_pairs));
    output[5] = finish();
    append_bytes(bytes, image.frame.prior_input_pairs.data(), sizeof(image.frame.prior_input_pairs));
    output[6] = finish();
    append_bytes(bytes, &image.frame.repeat_pending, sizeof(image.frame.repeat_pending));
    append_bytes(bytes, &image.frame.pending_move_state, sizeof(image.frame.pending_move_state));
    append_bytes(bytes, &image.frame.pending_dispatch, sizeof(image.frame.pending_dispatch));
    append_bytes(bytes, &image.frame.round_image_applied, sizeof(image.frame.round_image_applied));
    output[7] = finish();
    append_round_sequence(bytes, image.round_sequence); output[8] = finish();
    append_bytes(bytes, image.input_log.scalars.data(), image.input_log.scalars.size());
    output[9] = finish();
    constexpr std::size_t cache_blocks = 8;
    constexpr std::size_t rows_per_block =
        std::tuple_size_v<decltype(image.input_log.cache_rows)> / cache_blocks;
    for (std::size_t block = 0; block < cache_blocks; ++block)
    {
        const auto rows = std::span{image.input_log.cache_rows}.subspan(
            block * rows_per_block, rows_per_block);
        append_bytes(bytes, rows.data(), rows.size_bytes());
        output[10 + block] = finish();
    }
    append_bytes(bytes, image.move_dispatch_masks.data(), sizeof(image.move_dispatch_masks));
    output[18] = finish();
    append_bytes(bytes, image.vfx_edges.fighters.data(),
        sizeof(image.vfx_edges.fighters));
    output[29] = finish();
    append_bytes(bytes, image.movevm_state_shorts.fighters.data(),
        sizeof(image.movevm_state_shorts.fighters));
    output[30] = finish();
    append_bytes(bytes, image.pump.lane_a.data(), image.pump.lane_a.size());
    append_bytes(bytes, image.pump.lane_b.data(), image.pump.lane_b.size());
    append_bytes(bytes, image.pump.controls.data(), image.pump.controls.size());
    output[19] = finish();
    for (const auto& scheduler : image.schedulers)
    {
        append_bytes(bytes, scheduler.published_input.data(), scheduler.published_input.size());
        append_bytes(bytes, scheduler.command_state.data(), scheduler.command_state.size());
        append_bytes(bytes, scheduler.active_slot.data(), scheduler.active_slot.size());
    }
    output[20] = finish();
    for (const auto& subvm : image.sub_vms)
    {
        append_bytes(bytes, &subvm.vtable_rva, sizeof(subvm.vtable_rva));
        append_bytes(bytes, &subvm.extent, sizeof(subvm.extent));
        append_bytes(bytes, subvm.input_command.data(), subvm.input_command.size());
        append_bytes(bytes, subvm.common.data(), subvm.common.size());
        append_bytes(bytes, subvm.derived.data(), derived_size(subvm.extent));
    }
    output[21] = finish();
    for (const auto& command : image.move_commands)
        append_bytes(bytes, command.data(), command.size());
    output[22] = finish();
    for (const auto& param : image.slot_params)
        append_bytes(bytes, param.data(), param.size());
    output[23] = finish();
    append_bytes(bytes, &image.pending_hit.reaction_move_id, sizeof(image.pending_hit.reaction_move_id));
    append_bytes(bytes, &image.pending_hit.launcher_facing_delta, sizeof(image.pending_hit.launcher_facing_delta));
    append_bytes(bytes, &image.pending_hit.transition_flags, sizeof(image.pending_hit.transition_flags));
    append_bytes(bytes, &image.pending_hit.attacker_slot, sizeof(image.pending_hit.attacker_slot));
    append_bytes(bytes, &image.pending_hit.launcher_sync, sizeof(image.pending_hit.launcher_sync));
    output[24] = finish();
    append_bytes(bytes, &image.rng.lcg, sizeof(image.rng.lcg));
    append_bytes(bytes, image.rng.lfsr.data(), sizeof(image.rng.lfsr));
    append_bytes(bytes, &image.rng.lfsr_index, sizeof(image.rng.lfsr_index));
    append_bytes(bytes, image.rng.xorshift.data(), sizeof(image.rng.xorshift));
    append_bytes(bytes, image.rng.wind.data(), sizeof(image.rng.wind));
    output[25] = finish();
    append_bytes(bytes, image.vm_freeze_record.data(), image.vm_freeze_record.size());
    output[26] = finish();
    for (const auto& state : image.stage_wind_emitters.states)
        append_bytes(bytes, state.data(), state.size());
    output[27] = finish();
    // Component internals remain bounded, generation-checked local
    // reconstruction state. They are advanced by post-coordinate camera
    // presentation and camera-query matrix caches, so they are not a stable
    // peer-canonical boundary. The independently recovered PlayerWatch
    // distance history below remains the canonical historical input. Exact
    // per-batch camera publication identity is enforced by the ordered
    // presentation gate.
    for (const auto& history : image.camera_distance_history)
    {
        append_bytes(bytes, &history.present, sizeof(history.present));
        if (history.present == 0) continue;
        append_bytes(bytes, history.sample_bits.data(), sizeof(history.sample_bits));
        append_bytes(bytes, &history.sample_count, sizeof(history.sample_count));
        append_bytes(bytes, &history.cursor, sizeof(history.cursor));
    }
    output[28] = finish();
    return output;
}

Status NativeCandidateRegions::DecodeCanonicalBytes(
    std::span<const std::byte> bytes,
    NativeCandidateImage& output) noexcept
{
    reset_native_candidate(output);
    std::size_t cursor{};
    const auto take = [&bytes, &cursor](void* destination, std::size_t size) {
        if (size > bytes.size() - std::min(cursor, bytes.size())) return false;
        std::memcpy(destination, bytes.data() + cursor, size);
        cursor += size;
        return true;
    };
    if (!take(&output.session_generation, sizeof(output.session_generation))
        || !take(&output.round_generation, sizeof(output.round_generation))
        || output.session_generation == 0 || output.round_generation == 0
        || !take(&output.frame.frame_counter, sizeof(output.frame.frame_counter))
        || !take(&output.frame.input_game_round, sizeof(output.frame.input_game_round))
        || !take(&output.frame.input_game_time, sizeof(output.frame.input_game_time))
        || !take(&output.frame.manager_game_round_cursor,
            sizeof(output.frame.manager_game_round_cursor))
        || !take(&output.frame.manager_game_time_cursor,
            sizeof(output.frame.manager_game_time_cursor))
        || !take(&output.frame.round_state_frame,
            sizeof(output.frame.round_state_frame))
        || !take(&output.frame.unpause_countdown,
            sizeof(output.frame.unpause_countdown))
        || !take(output.frame.previous_inputs.data(),
            sizeof(output.frame.previous_inputs))
        || !take(output.frame.input_pairs.data(),
            sizeof(output.frame.input_pairs))
        || !take(output.frame.prior_input_pairs.data(),
            sizeof(output.frame.prior_input_pairs))
        || !take(&output.frame.repeat_pending,
            sizeof(output.frame.repeat_pending))
        || !take(&output.frame.pending_move_state,
            sizeof(output.frame.pending_move_state))
        || !take(&output.frame.pending_dispatch,
            sizeof(output.frame.pending_dispatch))
        || !take(&output.frame.round_image_applied,
            sizeof(output.frame.round_image_applied))
        || !take(&output.round_sequence.count,
            sizeof(output.round_sequence.count))
        || output.round_sequence.count > native_round_sequence_max_states
        || !take(&output.round_sequence.current_state,
            sizeof(output.round_sequence.current_state))
        || !take(output.round_sequence.states.data(), output.round_sequence.count)
        || !take(output.input_log.scalars.data(),
            output.input_log.scalars.size()))
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    for (auto& row : output.input_log.cache_rows)
    {
        if (!take(&row.game_round, sizeof(row.game_round))
            || !take(&row.frame_index, sizeof(row.frame_index))
            || !take(&row.input_value, sizeof(row.input_value))
            || !take(&row.filled, sizeof(row.filled))
            || row.filled > 1)
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    if (!take(output.move_dispatch_masks.data(),
            sizeof(output.move_dispatch_masks))
        || !take(output.vfx_edges.fighters.data(),
            sizeof(output.vfx_edges.fighters))
        || !take(output.movevm_state_shorts.fighters.data(),
            sizeof(output.movevm_state_shorts.fighters))
        || !take(output.pump.lane_a.data(), output.pump.lane_a.size())
        || !take(output.pump.lane_b.data(), output.pump.lane_b.size())
        || !take(output.pump.controls.data(), output.pump.controls.size()))
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    for (auto& scheduler : output.schedulers)
    {
        if (!take(scheduler.published_input.data(), scheduler.published_input.size())
            || !take(scheduler.command_state.data(), scheduler.command_state.size())
            || !take(scheduler.active_slot.data(), scheduler.active_slot.size()))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    for (auto& subvm : output.sub_vms)
    {
        if (!take(&subvm.vtable_rva, sizeof(subvm.vtable_rva))
            || !take(&subvm.extent, sizeof(subvm.extent))
            || extent_for_rva(subvm.vtable_rva) != subvm.extent
            || !take(subvm.input_command.data(), subvm.input_command.size())
            || !take(subvm.common.data(), subvm.common.size())
            || !take(subvm.derived.data(), derived_size(subvm.extent)))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::AdapterUnqualified);
        }
    }
    for (auto& command : output.move_commands)
    {
        if (!take(command.data(), command.size()))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    for (auto& param : output.slot_params)
    {
        if (!take(param.data(), param.size()))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    if (!take(&output.pending_hit.reaction_move_id,
            sizeof(output.pending_hit.reaction_move_id))
        || !take(&output.pending_hit.launcher_facing_delta,
            sizeof(output.pending_hit.launcher_facing_delta))
        || !take(&output.pending_hit.transition_flags,
            sizeof(output.pending_hit.transition_flags))
        || !take(&output.pending_hit.attacker_slot,
            sizeof(output.pending_hit.attacker_slot))
        || !take(&output.pending_hit.launcher_sync,
            sizeof(output.pending_hit.launcher_sync))
        || !valid_pending_hit(output.pending_hit)
        || !take(&output.rng.lcg, sizeof(output.rng.lcg))
        || !take(output.rng.lfsr.data(), sizeof(output.rng.lfsr))
        || !take(&output.rng.lfsr_index, sizeof(output.rng.lfsr_index))
        || output.rng.lfsr_index > output.rng.lfsr.size()
        || !take(output.rng.xorshift.data(), sizeof(output.rng.xorshift))
        || !take(output.rng.wind.data(), sizeof(output.rng.wind))
        || !take(output.vm_freeze_record.data(),
            output.vm_freeze_record.size())
        )
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    std::uint8_t emitter_count{};
    if (!take(&emitter_count, sizeof(emitter_count))
        || emitter_count > native_stage_wind_emitter_max_count)
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    try
    {
        output.stage_wind_emitters.states.reserve(
            native_stage_wind_emitter_max_count);
        output.stage_wind_emitters.states.resize(emitter_count);
    }
    catch (...)
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CapacityExceeded);
    }
    for (std::size_t index = 0;
         index < output.stage_wind_emitters.states.size(); ++index)
    {
        if (!take(output.stage_wind_emitters.states[index].data(),
                output.stage_wind_emitters.states[index].size()))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    // Camera component images are not decoded from peer-canonical bytes. They
    // remain zeroed and are therefore skipped by typed restore.
    for (auto& history : output.camera_distance_history)
    {
        if (!take(&history.present, sizeof(history.present))
            || history.present > 1
            || (history.present != 0
                && (!take(history.sample_bits.data(), sizeof(history.sample_bits))
                    || !take(&history.sample_count, sizeof(history.sample_count))
                    || !take(&history.cursor, sizeof(history.cursor))))
            || !valid_camera_history(history))
        {
            reset_native_candidate(output);
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    if (cursor != bytes.size())
    {
        reset_native_candidate(output);
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}
}
