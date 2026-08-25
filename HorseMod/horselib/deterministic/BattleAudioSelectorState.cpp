#include "BattleAudioSelectorState.hpp"

namespace Horse::Deterministic
{
namespace
{
template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
bool write_value(INativeMemory& memory, std::uintptr_t address, const T& value) noexcept
{
    return memory.Write(address, std::as_bytes(std::span{&value, 1}));
}
}

BattleAudioSelectorState::BattleAudioSelectorState(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

Status BattleAudioSelectorState::Bind(
    const BattleAudioSelectorBinding& binding) noexcept
{
    Reset();
    if (binding.image_base == 0 || binding.image_size == 0
        || binding.context.session_generation == 0
        || binding.context.round_generation == 0
        || binding.resolve_handler == nullptr
        || binding.handler_overflowed == nullptr)
    {
        return Status::failure(FailureCode::InvalidConfiguration);
    }
    binding_ = binding;
    bound_ = true;
    return Status::success();
}

void BattleAudioSelectorState::Reset() noexcept
{
    binding_ = {};
    bound_handlers_ = {};
    bound_ = false;
}

Status BattleAudioSelectorState::resolve_and_validate(
    std::size_t index, std::uintptr_t& handler, std::int32_t& alternation,
    bool allow_unobserved) noexcept
{
    if (index >= maximum_battle_audio_handlers
        || binding_.handler_overflowed(binding_.resolve_user))
    {
        return Status::failure(FailureCode::CapacityExceeded);
    }
    handler = binding_.resolve_handler(binding_.resolve_user, index);
    alternation = 0;
    if (handler == 0)
    {
        return allow_unobserved && bound_handlers_[index] == 0
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    if ((handler & (alignof(void*) - 1)) != 0
        || (bound_handlers_[index] != 0
            && handler != bound_handlers_[index]))
    {
        return Status::failure(FailureCode::IdentityMismatch);
    }
    std::uintptr_t vtable{};
    if (!read_value(memory_, handler, vtable)
        || vtable != binding_.image_base + handler_vtable_rva
        || !read_value(memory_, handler + alternation_offset, alternation))
    {
        return Status::failure(FailureCode::CapturePreflightFailed);
    }
    if (alternation < 0 || alternation > 1)
        return Status::failure(FailureCode::AdapterUnqualified);
    for (std::size_t prior = 0; prior < index; ++prior)
        if (bound_handlers_[prior] == handler)
            return Status::failure(FailureCode::IdentityMismatch);
    if (bound_handlers_[index] == 0) bound_handlers_[index] = handler;
    return Status::success();
}

Status BattleAudioSelectorState::Capture(
    BattleAudioSelectorImage& output) noexcept
{
    output = {};
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    output.session_generation = binding_.context.session_generation;
    output.round_generation = binding_.context.round_generation;
    for (std::size_t index = 0; index < maximum_battle_audio_handlers; ++index)
    {
        std::uintptr_t handler{};
        std::int32_t alternation{};
        const Status resolved = resolve_and_validate(
            index, handler, alternation, true);
        if (!resolved.ok()) return resolved;
        if (handler == 0) break;
        output.alternations[index] = alternation;
        ++output.observed_count;
    }
    return Status::success();
}

Status BattleAudioSelectorState::PreflightRestore(
    const BattleAudioSelectorImage& image) noexcept
{
    if (!bound_ || image.session_generation
            != binding_.context.session_generation
        || image.round_generation != binding_.context.round_generation)
        return Status::failure(FailureCode::GenerationMismatch);
    if (image.observed_count > maximum_battle_audio_handlers)
        return Status::failure(FailureCode::RestorePreflightFailed);
    for (std::size_t index = 0; index < maximum_battle_audio_handlers; ++index)
    {
        if (image.alternations[index] < 0 || image.alternations[index] > 1)
            return Status::failure(FailureCode::RestorePreflightFailed);
        std::uintptr_t handler{};
        std::int32_t current{};
        const Status resolved = resolve_and_validate(index, handler, current,
            index >= image.observed_count);
        if (!resolved.ok()) return resolved;
        if (index < image.observed_count && handler == 0)
            return Status::failure(FailureCode::IdentityMismatch);
        if (handler == 0) break;
    }
    return Status::success();
}

Status BattleAudioSelectorState::RestoreTransactional(
    const BattleAudioSelectorImage& image) noexcept
{
    const Status preflight = PreflightRestore(image);
    if (!preflight.ok()) return preflight;
    std::array<std::uintptr_t, maximum_battle_audio_handlers> handlers{};
    std::array<std::int32_t, maximum_battle_audio_handlers> undo{};
    std::size_t count{};
    for (; count < maximum_battle_audio_handlers; ++count)
    {
        const Status resolved = resolve_and_validate(
            count, handlers[count], undo[count],
            count >= image.observed_count);
        if (!resolved.ok()) return resolved;
        if (handlers[count] == 0) break;
    }
    std::size_t written{};
    for (; written < count; ++written)
    {
        const std::int32_t desired = written < image.observed_count
            ? image.alternations[written] : 0;
        if (!write_value(memory_, handlers[written] + alternation_offset,
                desired))
            break;
    }
    bool verified = written == count;
    for (std::size_t index = 0; verified && index < count; ++index)
    {
        std::int32_t observed{};
        const std::int32_t desired = index < image.observed_count
            ? image.alternations[index] : 0;
        verified = read_value(memory_, handlers[index] + alternation_offset,
            observed) && observed == desired;
    }
    if (verified) return Status::success();
    bool undone = true;
    while (written != 0)
    {
        --written;
        std::int32_t observed{};
        undone = write_value(memory_, handlers[written] + alternation_offset,
                undo[written])
            && read_value(memory_, handlers[written] + alternation_offset,
                observed)
            && observed == undo[written] && undone;
    }
    return Status::failure(undone
        ? FailureCode::RestoreVerificationFailed : FailureCode::UndoFailed);
}
}
