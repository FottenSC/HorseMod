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
        || binding.resolve_handler == nullptr)
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
    bound_handler_ = 0;
    bound_ = false;
}

Status BattleAudioSelectorState::resolve_and_validate(
    std::uintptr_t& handler, std::int32_t& alternation,
    bool allow_unobserved) noexcept
{
    handler = binding_.resolve_handler(binding_.resolve_user);
    alternation = 0;
    if (handler == 0)
    {
        return allow_unobserved && bound_handler_ == 0
            ? Status::success()
            : Status::failure(FailureCode::IdentityMismatch);
    }
    if ((handler & (alignof(void*) - 1)) != 0
        || (bound_handler_ != 0 && handler != bound_handler_))
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
    if (bound_handler_ == 0) bound_handler_ = handler;
    return Status::success();
}

Status BattleAudioSelectorState::Capture(
    BattleAudioSelectorImage& output) noexcept
{
    output = {};
    if (!bound_) return Status::failure(FailureCode::AdapterUnqualified);
    std::uintptr_t handler{};
    std::int32_t alternation{};
    const Status resolved = resolve_and_validate(handler, alternation, true);
    if (!resolved.ok()) return resolved;
    output.session_generation = binding_.context.session_generation;
    output.round_generation = binding_.context.round_generation;
    output.alternation = alternation;
    output.handler_observed = handler != 0;
    return Status::success();
}

Status BattleAudioSelectorState::PreflightRestore(
    const BattleAudioSelectorImage& image) noexcept
{
    if (!bound_ || image.session_generation
            != binding_.context.session_generation
        || image.round_generation != binding_.context.round_generation)
        return Status::failure(FailureCode::GenerationMismatch);
    if (image.alternation < 0 || image.alternation > 1)
        return Status::failure(FailureCode::RestorePreflightFailed);
    std::uintptr_t handler{};
    std::int32_t current{};
    const Status resolved = resolve_and_validate(
        handler, current, !image.handler_observed);
    if (!resolved.ok()) return resolved;
    if (image.handler_observed && handler == 0)
        return Status::failure(FailureCode::IdentityMismatch);
    return Status::success();
}

Status BattleAudioSelectorState::RestoreTransactional(
    const BattleAudioSelectorImage& image) noexcept
{
    const Status preflight = PreflightRestore(image);
    if (!preflight.ok()) return preflight;
    std::uintptr_t handler{};
    std::int32_t undo{};
    const Status resolved = resolve_and_validate(
        handler, undo, !image.handler_observed);
    if (!resolved.ok()) return resolved;
    if (handler == 0) return image.handler_observed
        ? Status::failure(FailureCode::IdentityMismatch)
        : Status::success();
    if (!write_value(memory_, handler + alternation_offset, image.alternation))
        return Status::failure(FailureCode::RestoreWriteFailed);
    std::int32_t observed{};
    if (read_value(memory_, handler + alternation_offset, observed)
        && observed == image.alternation)
    {
        return Status::success();
    }
    if (!write_value(memory_, handler + alternation_offset, undo)
        || !read_value(memory_, handler + alternation_offset, observed)
        || observed != undo)
    {
        return Status::failure(FailureCode::UndoFailed);
    }
    return Status::failure(FailureCode::RestoreVerificationFailed);
}
}
