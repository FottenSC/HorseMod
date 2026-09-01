#include "UcrtRandBroker.hpp"

#include "Schema.hpp"

namespace Horse::Deterministic
{
Status UcrtRandBroker::Start() noexcept
{
    Stop();
    image_.algorithm_version = Schema::Sc6UcrtLayout::algorithm_version;
    image_.allowlist_version = Schema::Sc6UcrtLayout::allowlist_version;
    mode_ = UcrtRandBrokerMode::Observing;
    return Status::success();
}

Status UcrtRandBroker::AcquireOwnership(std::uint32_t thread_id) noexcept
{
    if (mode_ != UcrtRandBrokerMode::Observing)
        return Status::failure(FailureCode::IllegalTransition);
    if (!RequireOwner(thread_id)) return Status::failure(failure_);
    if (!image_.seeded)
    {
        Fail(FailureCode::ContextUnavailable);
        return Status::failure(failure_);
    }
    mode_ = UcrtRandBrokerMode::Owned;
    return Status::success();
}

Status UcrtRandBroker::EnsureOwnership(std::uint32_t thread_id) noexcept
{
    if (mode_ == UcrtRandBrokerMode::Owned)
    {
        return owner_thread_id_ == thread_id
            ? Status::success()
            : Status::failure(FailureCode::WrongThread);
    }
    return AcquireOwnership(thread_id);
}

Status UcrtRandBroker::ReleaseOwnership(std::uint32_t thread_id) noexcept
{
    if (mode_ == UcrtRandBrokerMode::Observing)
        return Status::success();
    if (mode_ != UcrtRandBrokerMode::Owned || !RequireOwner(thread_id))
        return Status::failure(failure_ == FailureCode::None
            ? FailureCode::IllegalTransition : failure_);
    // The original CRT is advanced on every intercepted call even while the
    // broker supplies its owned value, so both streams remain aligned.
    mode_ = UcrtRandBrokerMode::Observing;
    return Status::success();
}

void UcrtRandBroker::Stop() noexcept
{
    mode_ = UcrtRandBrokerMode::Disabled;
    failure_ = FailureCode::None;
    owner_thread_id_ = 0;
    image_ = {};
}

bool UcrtRandBroker::IsRandCallsite(std::uintptr_t return_rva) noexcept
{
    return return_rva == Schema::Sc6UcrtLayout::rng_init_rand_return_rva
        || return_rva == Schema::Sc6UcrtLayout::movevm_rand_return_rva;
}

std::uint32_t UcrtRandBroker::Advance(std::uint32_t& state) noexcept
{
    state = state * 214013u + 2531011u;
    return (state >> 16) & 0x7fffu;
}

bool UcrtRandBroker::RequireOwner(std::uint32_t thread_id) noexcept
{
    if (thread_id == owner_thread_id_) return true;
    Fail(FailureCode::WrongThread);
    return false;
}

void UcrtRandBroker::Fail(FailureCode code) noexcept
{
    if (failure_ == FailureCode::None) failure_ = code;
    mode_ = UcrtRandBrokerMode::Failed;
}

int UcrtRandBroker::HandleRand(std::uint32_t thread_id,
    std::uintptr_t return_rva, UcrtRandFn original) noexcept
{
    const int forwarded = original != nullptr ? original() : 0;
    if (mode_ == UcrtRandBrokerMode::Disabled
        || mode_ == UcrtRandBrokerMode::Failed || !IsRandCallsite(return_rva))
    {
        return forwarded;
    }
    if (!RequireOwner(thread_id) || !image_.seeded)
    {
        if (!image_.seeded && mode_ != UcrtRandBrokerMode::Failed)
            Fail(FailureCode::ContextUnavailable);
        return forwarded;
    }
    const int deterministic = static_cast<int>(Advance(image_.state));
    ++image_.draws;
    return mode_ == UcrtRandBrokerMode::Owned ? deterministic : forwarded;
}

void UcrtRandBroker::HandleSrand(std::uint32_t thread_id,
    std::uintptr_t return_rva, unsigned int seed, UcrtSrandFn original) noexcept
{
    if (original != nullptr) original(seed);
    if (mode_ == UcrtRandBrokerMode::Disabled
        || mode_ == UcrtRandBrokerMode::Failed
        || return_rva != Schema::Sc6UcrtLayout::rng_init_srand_return_rva)
    {
        return;
    }
    if (owner_thread_id_ == 0) owner_thread_id_ = thread_id;
    if (!RequireOwner(thread_id)) return;
    image_.state = seed;
    image_.draws = 0;
    image_.seeded = true;
}

Status UcrtRandBroker::Capture(
    std::uint32_t thread_id, UcrtRandBrokerImage& output) noexcept
{
    output = {};
    if (mode_ == UcrtRandBrokerMode::Disabled
        || mode_ == UcrtRandBrokerMode::Failed || !RequireOwner(thread_id)
        || !image_.seeded)
    {
        return Status::failure(failure_ == FailureCode::None
            ? FailureCode::ContextUnavailable : failure_);
    }
    output = image_;
    return Status::success();
}

Status UcrtRandBroker::Restore(
    std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept
{
    if (mode_ != UcrtRandBrokerMode::Owned || !RequireOwner(thread_id))
        return Status::failure(failure_ == FailureCode::None
            ? FailureCode::IllegalTransition : failure_);
    if (!image.seeded
        || image.algorithm_version != Schema::Sc6UcrtLayout::algorithm_version
        || image.allowlist_version != Schema::Sc6UcrtLayout::allowlist_version)
    {
        Fail(FailureCode::RestorePreflightFailed);
        return Status::failure(failure_);
    }
    image_ = image;
    return Status::success();
}
}
