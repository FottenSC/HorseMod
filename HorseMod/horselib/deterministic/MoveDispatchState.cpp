#include "MoveDispatchState.hpp"

#include <cstring>
#include <span>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& value) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&value, 1}));
}

template <typename T>
bool write_value(INativeMemory& memory, std::uintptr_t address, const T& value) noexcept
{
    return memory.Write(address, std::as_bytes(std::span{&value, 1}));
}

void append(std::vector<std::byte>& out, const void* value, std::size_t size)
{
    const auto* first = static_cast<const std::byte*>(value);
    out.insert(out.end(), first, first + size);
}
}

MoveDispatchState::MoveDispatchState(INativeMemory& memory) noexcept : memory_(memory) {}

bool MoveDispatchState::capture_identity(Identity& output) noexcept
{
    output = {};
    std::uint8_t dirty{};
    if (!read_value(memory_, object_ + 0x470, output.frame_slot_table)
        || !read_value(memory_, object_ + 0x490, dirty)
        || !read_value(memory_, object_ + 0x498, output.sub_elements)
        || !read_value(memory_, object_ + 0x4A0, output.sub_element_count)
        || !read_value(memory_, object_ + 0x4A4, output.sub_element_capacity)
        || output.frame_slot_table == 0
        || (output.sub_element_count > 0 && output.sub_elements == 0)
        || output.sub_element_count < 0
        || output.sub_element_count > maximum_sub_elements
        || output.sub_element_capacity < 0
        || output.sub_element_capacity < output.sub_element_count)
    {
        return false;
    }
    output.pending_phase = dirty != 0;
    if (!output.pending_phase) return true;
    std::int32_t count{};
    if (!read_value(memory_, object_ + 0x480, output.pending_windows)
        || !read_value(memory_, object_ + 0x488, count)
        || !read_value(memory_, object_ + 0x48C, output.pending_capacity)
        || output.pending_windows == 0 || count < 0
        || count > maximum_pending_windows
        || output.pending_capacity < count)
    {
        return false;
    }
    return true;
}

bool MoveDispatchState::identity_matches(const Identity& expected) noexcept
{
    Identity current{};
    if (!capture_identity(current)) return false;
    if (current.frame_slot_table != expected.frame_slot_table
        || current.sub_elements != expected.sub_elements
        || current.sub_element_count != expected.sub_element_count
        || current.sub_element_capacity != expected.sub_element_capacity
        || current.pending_phase != expected.pending_phase)
    {
        return false;
    }
    return !current.pending_phase
        || (current.pending_windows == expected.pending_windows
            && current.pending_capacity == expected.pending_capacity);
}

Status MoveDispatchState::Bind(
    std::uintptr_t object, std::uint64_t generation) noexcept
{
    Invalidate();
    if (object == 0 || generation == 0)
        return Status::failure(FailureCode::ContextUnavailable);
    object_ = object;
    generation_ = generation;
    if (!capture_identity(identity_))
    {
        Invalidate();
        return Status::failure(FailureCode::AdapterUnqualified);
    }
    bound_ = true;
    MoveDispatchImage ignored{};
    if (!capture_unchecked(ignored))
    {
        Invalidate();
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    return Status::success();
}

void MoveDispatchState::Invalidate() noexcept
{
    object_ = 0;
    generation_ = 0;
    identity_ = {};
    bound_ = false;
}

bool MoveDispatchState::capture_unchecked(MoveDispatchImage& output) noexcept
{
    output.generation = generation_;
    output.frame_slot_index = 0;
    output.sub_frame_index = 0;
    output.saved_input_and_gates = 0;
    output.completion_delay = 0;
    if (!read_value(memory_, object_ + 0x478, output.frame_slot_index)
        || !read_value(memory_, object_ + 0x47C, output.sub_frame_index)
        || !read_value(memory_, object_ + 0x490, output.saved_input_and_gates)
        || !read_value(memory_, object_ + 0x494, output.completion_delay))
    {
        return false;
    }
    if (identity_.pending_phase)
    {
        std::int32_t count{};
        if (!read_value(memory_, object_ + 0x488, count)
            || count < 0 || count > maximum_pending_windows)
        {
            return false;
        }
        if (!std::holds_alternative<MoveDispatchPendingState>(output.phase))
            output.phase = MoveDispatchPendingState{
                std::move(output.pending_windows_scratch)};
        auto& pending = std::get<MoveDispatchPendingState>(output.phase);
        try
        {
            pending.windows.reserve(maximum_pending_windows);
            pending.windows.resize(static_cast<std::size_t>(count));
        }
        catch (...) { return false; }
        if (count != 0
            && !memory_.Read(identity_.pending_windows,
                std::as_writable_bytes(std::span{pending.windows})))
        {
            return false;
        }
    }
    else
    {
        if (auto* pending =
                std::get_if<MoveDispatchPendingState>(&output.phase))
            output.pending_windows_scratch = std::move(pending->windows);
        MoveDispatchActionModeState state{};
        if (!read_value(memory_, object_ + 0x480, state.action_mode)
            || !read_value(memory_, object_ + 0x484, state.frame_counter)
            || !read_value(memory_, object_ + 0x488, state.pending_window_gate)
            || state.action_mode > 9 || state.pending_window_gate > 4)
        {
            return false;
        }
        output.phase = state;
    }
    try
    {
        output.sub_elements.reserve(maximum_sub_elements);
        output.sub_elements.resize(
            static_cast<std::size_t>(identity_.sub_element_count));
    }
    catch (...) { return false; }
    for (std::size_t i = 0; i < output.sub_elements.size(); ++i)
    {
        const auto address = identity_.sub_elements + i * 0x20;
        if (!read_value(memory_, address, output.sub_elements[i].tick_count)
            || !read_value(memory_, address + 4, output.sub_elements[i].complete)
            || output.sub_elements[i].complete > 1)
        {
            return false;
        }
    }
    return true;
}

Status MoveDispatchState::Capture(MoveDispatchImage& output) noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    if (!identity_matches(identity_))
        return Status::failure(FailureCode::IdentityMismatch);
    return capture_unchecked(output)
        ? Status::success()
        : Status::failure(FailureCode::CaptureFailed);
}

Status MoveDispatchState::PreflightRestore(
    const MoveDispatchImage& image) noexcept
{
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    if (image.generation != generation_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (image.sub_elements.size()
        != static_cast<std::size_t>(identity_.sub_element_count))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    const bool target_pending =
        std::holds_alternative<MoveDispatchPendingState>(image.phase);
    if (target_pending != identity_.pending_phase)
        return Status::failure(FailureCode::GenerationMismatch);
    const bool encoded_pending = (image.saved_input_and_gates & 0xFFU) != 0;
    if (encoded_pending != target_pending)
        return Status::failure(FailureCode::RestorePreflightFailed);
    if (target_pending
        && std::get<MoveDispatchPendingState>(image.phase).windows.size()
            > static_cast<std::size_t>(identity_.pending_capacity))
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return identity_matches(identity_)
        ? Status::success()
        : Status::failure(FailureCode::IdentityMismatch);
}

bool MoveDispatchState::write_image(
    const MoveDispatchImage& image, bool reverse) noexcept
{
    const auto write_sub_elements = [&]() noexcept {
        bool ok = true;
        for (std::size_t n = image.sub_elements.size(); n-- > 0;)
        {
            const auto i = reverse ? n : image.sub_elements.size() - 1 - n;
            const auto address = identity_.sub_elements + i * 0x20;
            ok = write_value(memory_, address, image.sub_elements[i].tick_count) && ok;
            ok = write_value(memory_, address + 4, image.sub_elements[i].complete) && ok;
        }
        return ok;
    };
    if (reverse && !write_sub_elements()) return false;
    if (!write_value(memory_, object_ + 0x478, image.frame_slot_index)
        || !write_value(memory_, object_ + 0x47C, image.sub_frame_index))
    {
        return false;
    }
    if (const auto* pending = std::get_if<MoveDispatchPendingState>(&image.phase))
    {
        if (!pending->windows.empty()
            && !memory_.Write(identity_.pending_windows,
                std::as_bytes(std::span{pending->windows})))
        {
            return false;
        }
        const auto count = static_cast<std::int32_t>(pending->windows.size());
        if (!write_value(memory_, object_ + 0x488, count)) return false;
    }
    else
    {
        const auto& state = std::get<MoveDispatchActionModeState>(image.phase);
        if (!write_value(memory_, object_ + 0x480, state.action_mode)
            || !write_value(memory_, object_ + 0x484, state.frame_counter)
            || !write_value(memory_, object_ + 0x488, state.pending_window_gate))
        {
            return false;
        }
    }
    if (!write_value(memory_, object_ + 0x490, image.saved_input_and_gates)
        || !write_value(memory_, object_ + 0x494, image.completion_delay))
    {
        return false;
    }
    return reverse || write_sub_elements();
}

Status MoveDispatchState::RestoreTransactional(
    const MoveDispatchImage& image) noexcept
{
    const auto preflight = PreflightRestore(image);
    if (!preflight.ok()) return preflight;
    if (!capture_unchecked(restore_undo_scratch_))
        return Status::failure(FailureCode::CaptureFailed);
    const bool wrote = write_image(image, false);
    if (wrote && capture_unchecked(restore_verification_scratch_)
        && restore_verification_scratch_ == image)
        return Status::success();
    if (!identity_matches(identity_)
        || !write_image(restore_undo_scratch_, true))
        return Status::failure(FailureCode::UndoFailed);
    if (!capture_unchecked(restore_verification_scratch_)
        || restore_verification_scratch_ != restore_undo_scratch_)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(
        wrote ? FailureCode::RestoreVerificationFailed
              : FailureCode::RestoreWriteFailed);
}

std::size_t MoveDispatchState::ScratchCapacityBytes() const noexcept
{
    const auto capacity = [](const MoveDispatchImage& image) noexcept {
        std::size_t bytes = image.sub_elements.capacity()
            * sizeof(MoveDispatchSubElementState);
        if (const auto* pending =
                std::get_if<MoveDispatchPendingState>(&image.phase))
            bytes += pending->windows.capacity()
                * sizeof(MoveDispatchPendingWindow);
        bytes += image.pending_windows_scratch.capacity()
            * sizeof(MoveDispatchPendingWindow);
        return bytes;
    };
    return capacity(restore_undo_scratch_)
        + capacity(restore_verification_scratch_);
}

Status MoveDispatchState::PrepareImageStorage(
    MoveDispatchImage& output) noexcept
{
    try
    {
        output.sub_elements.reserve(maximum_sub_elements);
        if (auto* pending =
                std::get_if<MoveDispatchPendingState>(&output.phase))
        {
            pending->windows.reserve(maximum_pending_windows);
        }
        output.pending_windows_scratch.reserve(maximum_pending_windows);
    }
    catch (...)
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return Status::success();
}

std::vector<std::byte> MoveDispatchState::CanonicalBytes(
    const MoveDispatchImage& image)
{
    std::vector<std::byte> output;
    CanonicalBytes(image, output);
    return output;
}

void MoveDispatchState::CanonicalBytes(
    const MoveDispatchImage& image, std::vector<std::byte>& output)
{
    output.clear();
    if (output.capacity() < 0x10000) output.reserve(0x10000);
    append(output, &image.generation, sizeof(image.generation));
    append(output, &image.frame_slot_index, sizeof(image.frame_slot_index));
    append(output, &image.sub_frame_index, sizeof(image.sub_frame_index));
    const std::uint8_t phase = std::holds_alternative<MoveDispatchPendingState>(
        image.phase) ? 1 : 0;
    append(output, &phase, sizeof(phase));
    if (const auto* pending = std::get_if<MoveDispatchPendingState>(&image.phase))
    {
        const auto count = static_cast<std::uint32_t>(pending->windows.size());
        append(output, &count, sizeof(count));
        for (const auto& window : pending->windows)
        {
            append(output, &window.owner_slot_tag, sizeof(window.owner_slot_tag));
            append(output, &window.payload_flags, sizeof(window.payload_flags));
            append(output, &window.payload_xy, sizeof(window.payload_xy));
            append(output, &window.payload_tail, sizeof(window.payload_tail));
            append(output, &window.start_frame, sizeof(window.start_frame));
            append(output, &window.end_frame, sizeof(window.end_frame));
        }
    }
    else
    {
        const auto& state = std::get<MoveDispatchActionModeState>(image.phase);
        append(output, &state.action_mode, sizeof(state.action_mode));
        append(output, &state.frame_counter, sizeof(state.frame_counter));
        append(output, &state.pending_window_gate,
            sizeof(state.pending_window_gate));
    }
    append(output, &image.saved_input_and_gates,
        sizeof(image.saved_input_and_gates));
    append(output, &image.completion_delay, sizeof(image.completion_delay));
    const auto element_count =
        static_cast<std::uint32_t>(image.sub_elements.size());
    append(output, &element_count, sizeof(element_count));
    for (const auto& element : image.sub_elements)
    {
        append(output, &element.tick_count, sizeof(element.tick_count));
        append(output, &element.complete, sizeof(element.complete));
    }
}

Status MoveDispatchState::DecodeCanonicalBytes(
    std::span<const std::byte> bytes, MoveDispatchImage& output) noexcept
{
    output.generation = 0;
    output.frame_slot_index = 0;
    output.sub_frame_index = 0;
    output.saved_input_and_gates = 0;
    output.completion_delay = 0;
    std::size_t cursor{};
    const auto take = [&](void* destination, std::size_t size) noexcept {
        if (cursor > bytes.size() || size > bytes.size() - cursor) return false;
        std::memcpy(destination, bytes.data() + cursor, size);
        cursor += size;
        return true;
    };
    std::uint8_t phase{};
    if (!take(&output.generation, sizeof(output.generation))
        || output.generation == 0
        || !take(&output.frame_slot_index, sizeof(output.frame_slot_index))
        || !take(&output.sub_frame_index, sizeof(output.sub_frame_index))
        || !take(&phase, sizeof(phase)) || phase > 1)
    {
        return Status::failure(FailureCode::CaptureFailed);
    }
    try
    {
        if (phase == 0)
        {
            if (auto* pending =
                    std::get_if<MoveDispatchPendingState>(&output.phase))
                output.pending_windows_scratch = std::move(pending->windows);
            MoveDispatchActionModeState state{};
            if (!take(&state.action_mode, sizeof(state.action_mode))
                || !take(&state.frame_counter, sizeof(state.frame_counter))
                || !take(&state.pending_window_gate,
                    sizeof(state.pending_window_gate))
                || state.action_mode > 9 || state.pending_window_gate > 4)
            {
                return Status::failure(FailureCode::CaptureFailed);
            }
            output.phase = state;
        }
        else
        {
            std::uint32_t count{};
            if (!take(&count, sizeof(count)) || count > maximum_pending_windows)
                return Status::failure(FailureCode::CaptureFailed);
            if (!std::holds_alternative<MoveDispatchPendingState>(output.phase))
                output.phase = MoveDispatchPendingState{
                    std::move(output.pending_windows_scratch)};
            auto& pending = std::get<MoveDispatchPendingState>(output.phase);
            pending.windows.reserve(maximum_pending_windows);
            pending.windows.resize(count);
            for (auto& window : pending.windows)
            {
                if (!take(&window.owner_slot_tag, sizeof(window.owner_slot_tag))
                    || !take(&window.payload_flags, sizeof(window.payload_flags))
                    || !take(&window.payload_xy, sizeof(window.payload_xy))
                    || !take(&window.payload_tail, sizeof(window.payload_tail))
                    || !take(&window.start_frame, sizeof(window.start_frame))
                    || !take(&window.end_frame, sizeof(window.end_frame)))
                {
                    return Status::failure(FailureCode::CaptureFailed);
                }
            }
        }
        std::uint32_t element_count{};
        if (!take(&output.saved_input_and_gates,
                sizeof(output.saved_input_and_gates))
            || !take(&output.completion_delay, sizeof(output.completion_delay))
            || !take(&element_count, sizeof(element_count))
            || element_count > maximum_sub_elements)
        {
            return Status::failure(FailureCode::CaptureFailed);
        }
        output.sub_elements.reserve(maximum_sub_elements);
        output.sub_elements.resize(element_count);
        for (auto& element : output.sub_elements)
        {
            if (!take(&element.tick_count, sizeof(element.tick_count))
                || !take(&element.complete, sizeof(element.complete))
                || element.complete > 1)
            {
                return Status::failure(FailureCode::CaptureFailed);
            }
        }
    }
    catch (...)
    {
        output.generation = 0;
        output.sub_elements.clear();
        if (auto* pending =
                std::get_if<MoveDispatchPendingState>(&output.phase))
            pending->windows.clear();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    return cursor == bytes.size()
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}
}
