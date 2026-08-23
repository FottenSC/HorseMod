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
    std::int32_t mask_count{};
    std::int32_t mask_capacity{};
    if (!read_value(memory_, addresses_.move_dispatch + 0x4A8, output.event_mask_owner)
        || !read_value(memory_, addresses_.move_dispatch + 0x4B0, mask_count)
        || !read_value(memory_, addresses_.move_dispatch + 0x4B4, mask_capacity)
        || output.event_mask_owner == 0 || mask_count != 2 || mask_capacity != 2)
    {
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
        if (!read_value(memory_, identity.scheduler + 0x50, identity.object)
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
            || identity.owner_scheduler != identity.scheduler)
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
    return capture_identities(current) && current.event_mask_owner == identities_.event_mask_owner
        && current.pump == identities_.pump && current.move_commands == identities_.move_commands
        && std::equal(
            current.sub_vms.begin(), current.sub_vms.end(), identities_.sub_vms.begin(),
            [](const SubVmIdentity& a, const SubVmIdentity& b) {
                return a.scheduler == b.scheduler && a.object == b.object
                    && a.vtable == b.vtable && a.fighter == b.fighter
                    && a.opponent == b.opponent
                    && a.owner_scheduler == b.owner_scheduler && a.extent == b.extent;
            });
}

Status NativeCandidateRegions::Bind(const NativeCandidateAddresses& addresses) noexcept
{
    Invalidate();
    if (addresses.image_base == 0 || addresses.move_dispatch == 0
        || addresses.pump_state == 0 || addresses.scheduler_base == 0
        || addresses.move_command_base == 0 || addresses.slot_param_base == 0
        || addresses.session_generation == 0 || addresses.round_generation == 0)
    {
        return Status::failure(FailureCode::ContextUnavailable);
    }
    addresses_ = addresses;
    if (!capture_identities(identities_))
    {
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
    return identities_match()
        ? Status::success()
        : Status::failure(FailureCode::IdentityMismatch);
}

bool NativeCandidateRegions::capture_unchecked(NativeCandidateImage& output) noexcept
{
    output = {};
    output.session_generation = addresses_.session_generation;
    output.round_generation = addresses_.round_generation;
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
    return true;
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
    if (!write_bytes(
            identities_.event_mask_owner,
            std::as_bytes(std::span{image.move_dispatch_masks}))) return false;
    if (!write_bytes(addresses_.pump_state + 0x20, image.pump.lane_a)
        || !write_bytes(addresses_.pump_state + 0x50, image.pump.lane_b)
        || !write_bytes(addresses_.pump_state + 0x70, image.pump.controls)) return false;
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
    ok = write_bytes(addresses_.pump_state + 0x70, image.pump.controls) && ok;
    ok = write_bytes(addresses_.pump_state + 0x50, image.pump.lane_b) && ok;
    ok = write_bytes(addresses_.pump_state + 0x20, image.pump.lane_a) && ok;
    ok = write_bytes(
        identities_.event_mask_owner,
        std::as_bytes(std::span{image.move_dispatch_masks})) && ok;
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
    append_bytes(output, image.move_dispatch_masks.data(), sizeof(image.move_dispatch_masks));
    append_bytes(output, image.pump.lane_a.data(), image.pump.lane_a.size());
    append_bytes(output, image.pump.lane_b.data(), image.pump.lane_b.size());
    append_bytes(output, image.pump.controls.data(), image.pump.controls.size());
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
    return output;
}
}
