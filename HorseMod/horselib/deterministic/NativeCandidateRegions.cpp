#include "NativeCandidateRegions.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Horse::Deterministic
{
namespace
{
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
constexpr std::ptrdiff_t manager_input_log = 0x478;
constexpr std::ptrdiff_t manager_repeat_pending = 0x1462;
constexpr std::ptrdiff_t manager_pending_move_state = 0x1463;
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
}

void append_input_log(
    std::vector<std::byte>& output,
    const NativeFrameInputLogImage& input_log)
{
    append_bytes(output, input_log.scalars.data(), input_log.scalars.size());
    for (const auto& row : input_log.cache_rows)
    {
        append_bytes(output, &row.game_round, sizeof(row.game_round));
        append_bytes(output, &row.frame_index, sizeof(row.frame_index));
        append_bytes(output, &row.input_value, sizeof(row.input_value));
        append_bytes(output, &row.filled, sizeof(row.filled));
    }
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
        && current.event_mask_owner == identities_.event_mask_owner
        && current.pump == identities_.pump && current.move_commands == identities_.move_commands
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

bool NativeCandidateRegions::capture_unchecked(NativeCandidateImage& output) noexcept
{
    output = {};
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
    if (!read_bytes(identities_.previous_input_array,
            std::as_writable_bytes(std::span{output.frame.previous_inputs})))
        return region_read_failed(10);
    if (!read_bytes(identities_.input_pair_array,
            std::as_writable_bytes(std::span{output.frame.input_pairs})))
        return region_read_failed(11);
    if (!read_bytes(identities_.prior_input_pair_array,
            std::as_writable_bytes(std::span{output.frame.prior_input_pairs})))
        return region_read_failed(12);
    if (!validate_input_log_image(
            output.input_log, output.frame, validation_diagnostic_)) return false;
    if (!read_bytes(
            identities_.event_mask_owner,
            std::as_writable_bytes(std::span{output.move_dispatch_masks})))
    {
        return false;
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
    return image.rng.lfsr_index <= image.rng.lfsr.size();
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
        || !write_bytes(addresses_.frame_counter,
            std::as_bytes(std::span{&image.frame.frame_counter, 1}))
        || !write_bytes(addresses_.wind_rng,
            std::as_bytes(std::span{image.rng.wind}))
        || !write_bytes(addresses_.xorshift_rng,
            std::as_bytes(std::span{image.rng.xorshift}))
        || !write_bytes(addresses_.lfsr_rng,
            std::as_bytes(std::span{image.rng.lfsr}))
        || !write_bytes(addresses_.lfsr_rng + 0x64,
            std::as_bytes(std::span{&image.rng.lfsr_index, 1}))
        || !write_bytes(addresses_.lcg_rng,
            std::as_bytes(std::span{&image.rng.lcg, 1})))
    {
        return false;
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
    return true;
}

bool NativeCandidateRegions::write_reverse(const NativeCandidateImage& image) noexcept
{
    bool ok = true;
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
    ok = write_bytes(addresses_.frame_counter,
        std::as_bytes(std::span{&image.frame.frame_counter, 1})) && ok;
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
    NativeCandidateImage undo{};
    if (!capture_unchecked(undo)) return Status::failure(FailureCode::CaptureFailed);
    const bool wrote = write_forward(image);
    NativeCandidateImage verification{};
    const bool verified = wrote && capture_unchecked(verification) && verification == image;
    if (verified) return Status::success();
    if (!identities_match() || !write_reverse(undo))
        return Status::failure(FailureCode::UndoFailed);
    NativeCandidateImage undo_verification{};
    if (!capture_unchecked(undo_verification) || undo_verification != undo)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(
        wrote ? FailureCode::RestoreVerificationFailed : FailureCode::RestoreWriteFailed);
}

std::vector<std::byte> NativeCandidateRegions::CanonicalBytes(
    const NativeCandidateImage& image)
{
    std::vector<std::byte> output;
    output.reserve(0x6100);
    append_bytes(output, &image.session_generation, sizeof(image.session_generation));
    append_bytes(output, &image.round_generation, sizeof(image.round_generation));
    append_frame_boundary(output, image.frame);
    append_input_log(output, image.input_log);
    append_bytes(output, image.move_dispatch_masks.data(), sizeof(image.move_dispatch_masks));
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
    append_bytes(output, &image.rng.lcg, sizeof(image.rng.lcg));
    append_bytes(output, image.rng.lfsr.data(), sizeof(image.rng.lfsr));
    append_bytes(output, &image.rng.lfsr_index, sizeof(image.rng.lfsr_index));
    append_bytes(output, image.rng.xorshift.data(), sizeof(image.rng.xorshift));
    append_bytes(output, image.rng.wind.data(), sizeof(image.rng.wind));
    return output;
}

Status NativeCandidateRegions::DecodeCanonicalBytes(
    std::span<const std::byte> bytes,
    NativeCandidateImage& output) noexcept
{
    output = {};
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
        || !take(output.input_log.scalars.data(),
            output.input_log.scalars.size()))
    {
        output = {};
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
            output = {};
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    if (!take(output.move_dispatch_masks.data(),
            sizeof(output.move_dispatch_masks))
        || !take(output.pump.lane_a.data(), output.pump.lane_a.size())
        || !take(output.pump.lane_b.data(), output.pump.lane_b.size())
        || !take(output.pump.controls.data(), output.pump.controls.size()))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    for (auto& scheduler : output.schedulers)
    {
        if (!take(scheduler.published_input.data(), scheduler.published_input.size())
            || !take(scheduler.command_state.data(), scheduler.command_state.size())
            || !take(scheduler.active_slot.data(), scheduler.active_slot.size()))
        {
            output = {};
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
            output = {};
            return Status::failure(FailureCode::AdapterUnqualified);
        }
    }
    for (auto& command : output.move_commands)
    {
        if (!take(command.data(), command.size()))
        {
            output = {};
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    for (auto& param : output.slot_params)
    {
        if (!take(param.data(), param.size()))
        {
            output = {};
            return Status::failure(FailureCode::CaptureFailed);
        }
    }
    if (!take(&output.rng.lcg, sizeof(output.rng.lcg))
        || !take(output.rng.lfsr.data(), sizeof(output.rng.lfsr))
        || !take(&output.rng.lfsr_index, sizeof(output.rng.lfsr_index))
        || output.rng.lfsr_index > output.rng.lfsr.size()
        || !take(output.rng.xorshift.data(), sizeof(output.rng.xorshift))
        || !take(output.rng.wind.data(), sizeof(output.rng.wind)))
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    if (cursor != bytes.size())
    {
        output = {};
        return Status::failure(FailureCode::CaptureFailed);
    }
    return Status::success();
}
}
