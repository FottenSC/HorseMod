#include "UcrtRandBroker.hpp"

#include "Schema.hpp"
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Horse::Deterministic
{
namespace
{
std::uint32_t NextCrt(std::uint32_t state) noexcept
{
    return state * 214013u + 2531011u;
}
bool IsNativePresentation(std::uintptr_t rva) noexcept
{
    // Classification is diagnostic only. All other callers stay native and
    // authoritative/unresolved; this list grants no checkpoint exclusion.
    return rva == 0x895d6e || rva == 0x896105
        || rva == 0x1f9bd5c || rva == 0x54f91e;
}
bool ValidWarmup(const UcrtRandBrokerImage& image) noexcept
{
    // srand receives seed >> 4: only the high eight bits of seed & 0xfff
    // survive. Do NOT invent the low nibble or certify completion by count.
    // Completion is supplied by the native post-warmup boundary below.
    const auto minimum = (image.seed_state & 0xffu) << 4;
    return image.seed_state <= 0x0fffffffu && image.warmup_draws >= minimum
        && image.warmup_draws <= minimum + 15;
}
}
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
    const auto ready = CompleteInitialization(thread_id);
    if (!ready.ok()) return ready;
    auto expected = UcrtRandBrokerMode::Observing;
    if (!mode_.compare_exchange_strong(expected, UcrtRandBrokerMode::Owned))
        return Status::failure(FailureCode::IllegalTransition);
    return Status::success();
}

Status UcrtRandBroker::EnsureOwnership(std::uint32_t thread_id) noexcept
{
    if (!IsOwner(thread_id)) return Status::failure(FailureCode::WrongThread);
    if (mode_ != UcrtRandBrokerMode::Owned && mode_ != UcrtRandBrokerMode::Observing)
        return Status::failure(FailureCode::IllegalTransition);
    if (!ValidateImage(image_)) return Status::failure(FailureCode::ContextUnavailable);
    auto expected = UcrtRandBrokerMode::Observing;
    if (!mode_.compare_exchange_strong(expected, UcrtRandBrokerMode::Owned)
        && expected != UcrtRandBrokerMode::Owned)
        return Status::failure(FailureCode::IllegalTransition);
    return Status::success();
}

Status UcrtRandBroker::CompleteInitialization(std::uint32_t thread_id) noexcept
{
    if (!IsOwner(thread_id)) return Status::failure(FailureCode::WrongThread);
    if (!image_.seeded) return Status::failure(FailureCode::ContextUnavailable);
    if (image_.combat_ready) return Status::success();
    std::uint32_t native{};
    if (!ValidWarmup(image_) || !ReadNativeState(native) || native != image_.state)
        return Status::failure(FailureCode::RestorePreflightFailed);
    // Clone the observed cursor, never an expected observation or draw count.
    image_.combat_state = native;
    image_.combat_ready = true;
    return Status::success();
}

Status UcrtRandBroker::ObserveInitializationComplete(
    std::uint32_t thread_id, std::uintptr_t return_rva) noexcept
{
    if (return_rva != Schema::Sc6UcrtLayout::rng_init_xorshift_return_rva
        || mode_ != UcrtRandBrokerMode::Observing)
        return Status::failure(FailureCode::IllegalTransition);
    const auto result = CompleteInitialization(thread_id);
    if (!result.ok()) Fail(result.code);
    return result;
}

Status UcrtRandBroker::ReleaseOwnership(std::uint32_t thread_id) noexcept
{
    if (mode_ == UcrtRandBrokerMode::Observing && owner_thread_id_ == 0)
        return Status::success();
    if (!IsOwner(thread_id)) return Status::failure(FailureCode::WrongThread);
    if (mode_ != UcrtRandBrokerMode::Owned && mode_ != UcrtRandBrokerMode::Observing)
        return Status::failure(failure() == FailureCode::None
            ? FailureCode::IllegalTransition : failure());
    // Release restore permission only. Merging either cursor into the other
    // would change both continuations. Keep routing the established split.
    auto expected = UcrtRandBrokerMode::Owned;
    if (!mode_.compare_exchange_strong(expected, UcrtRandBrokerMode::Observing)
        && expected != UcrtRandBrokerMode::Observing)
        return Status::failure(FailureCode::IllegalTransition);
    return Status::success();
}

void UcrtRandBroker::Stop() noexcept
{
    mode_ = UcrtRandBrokerMode::Disabled;
    failure_ = FailureCode::None;
    owner_thread_id_ = 0;
    image_ = {}; // next_epoch_ intentionally survives Stop/Start (stale images).
    native_get_ptd_ = nullptr;
    native_seed_ = nullptr;
    native_state_ = nullptr;
    native_thread_ = 0;
}

Status UcrtRandBroker::BindNative(UcrtRandFn random, UcrtSrandFn seed) noexcept
{
#ifdef _WIN32
    if (mode_ != UcrtRandBrokerMode::Observing || native_get_ptd_ || !random || !seed)
        return Status::failure(FailureCode::IllegalTransition);
    // Verified ucrtbase 5e7709a6...: both exports call the same PTD getter;
    // rand uses PTD+28 with 214013/2531011, srand writes its argument there.
    // Reject a changed implementation instead of guessing its TLS layout.
    __try
    {
        HMODULE module{}, seed_module{}, getter_module{};
        constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(random), &module)
            || !GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(seed), &seed_module)
            || module != seed_module || GetProcAddress(module, "rand") != reinterpret_cast<FARPROC>(random)
            || GetProcAddress(module, "srand") != reinterpret_cast<FARPROC>(seed))
            return Status::failure(FailureCode::IdentityMismatch);
        const auto* r = reinterpret_cast<const unsigned char*>(random);
        const auto* s = reinterpret_cast<const unsigned char*>(seed);
        constexpr unsigned char prefix_r[]{0x48,0x83,0xec,0x28,0xe8};
        constexpr unsigned char suffix_r[]{0x69,0x48,0x28,0xfd,0x43,0x03,0x00,0x81,0xc1,0xc3,0x9e,0x26,0x00,
            0x89,0x48,0x28,0xc1,0xe9,0x10,0x81,0xe1,0xff,0x7f,0x00,0x00,0x8b,0xc1,0x48,0x83,0xc4,0x28,0xc3};
        constexpr unsigned char prefix_s[]{0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0xd9,0xe8};
        constexpr unsigned char suffix_s[]{0x89,0x58,0x28,0x48,0x83,0xc4,0x20,0x5b,0xc3};
        if (std::memcmp(r, prefix_r, sizeof(prefix_r)) || std::memcmp(r + 9, suffix_r, sizeof(suffix_r))
            || std::memcmp(s, prefix_s, sizeof(prefix_s)) || std::memcmp(s + 13, suffix_s, sizeof(suffix_s)))
            return Status::failure(FailureCode::UnsupportedContent);
        std::int32_t r_delta{}, s_delta{};
        std::memcpy(&r_delta, r + 5, sizeof(r_delta));
        std::memcpy(&s_delta, s + 9, sizeof(s_delta));
        const auto* getter = r + 9 + r_delta;
        if (getter != s + 13 + s_delta
            || !GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(getter), &getter_module)
            || getter_module != module)
            return Status::failure(FailureCode::IdentityMismatch);
        native_get_ptd_ = reinterpret_cast<void* (*)()>(const_cast<unsigned char*>(getter));
        native_seed_ = seed;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
#else
    return Status::failure(FailureCode::UnsupportedContent);
#endif
}

bool UcrtRandBroker::ReadNativeState(std::uint32_t& state) noexcept
{
#ifdef _WIN32
    if (!native_get_ptd_ || !native_seed_ || (native_thread_ && native_thread_ != GetCurrentThreadId())) return false;
    __try
    {
        auto* ptd = static_cast<std::byte*>(native_get_ptd_());
        if (!ptd) return false;
        auto* current = reinterpret_cast<std::uint32_t*>(ptd + 0x28);
        if (native_state_ && current != native_state_) return false;
        state = *current;
        native_state_ = current;
        native_thread_ = GetCurrentThreadId();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    return false;
#endif
}

bool UcrtRandBroker::IsOwner(std::uint32_t thread_id) const noexcept
{
    return thread_id != 0 && thread_id == owner_thread_id_
#ifdef _WIN32
        && thread_id == GetCurrentThreadId()
#endif
        ;
}

Status UcrtRandBroker::ObserveNative(std::uint32_t thread_id, std::uint32_t& state) noexcept
{
    state = 0;
    if (mode_ != UcrtRandBrokerMode::Observing)
        return Status::failure(FailureCode::IllegalTransition);
#ifdef _WIN32
    if (thread_id != GetCurrentThreadId() || (native_thread_ && thread_id != native_thread_))
        return Status::failure(FailureCode::WrongThread);
#endif
    return ReadNativeState(state) ? Status::success()
        : Status::failure(FailureCode::ContextUnavailable);
}

bool UcrtRandBroker::RequireOwner(std::uint32_t thread_id) noexcept
{
    if (IsOwner(thread_id)) return true;
    Fail(FailureCode::WrongThread);
    return false;
}

void UcrtRandBroker::Fail(FailureCode code) noexcept
{
    auto expected = FailureCode::None;
    failure_.compare_exchange_strong(expected, code);
    mode_ = UcrtRandBrokerMode::Failed;
}

int UcrtRandBroker::HandleRand(std::uint32_t thread_id,
    std::uintptr_t return_rva, UcrtRandFn original) noexcept
{
    const auto forward = [&] { return original ? original() : 0; };
    if (mode_ == UcrtRandBrokerMode::Disabled || mode_ == UcrtRandBrokerMode::Failed)
        return forward();
    const bool movevm = return_rva == Schema::Sc6UcrtLayout::movevm_rand_return_rva;
    const bool warmup = return_rva == Schema::Sc6UcrtLayout::rng_init_rand_return_rva;
    if (!IsOwner(thread_id))
    {
        if (movevm || warmup) Fail(FailureCode::WrongThread);
        return forward(); // Unrelated foreign TLS is never a lane in this image.
    }
    if (movevm)
    {
        if (!image_.combat_ready || image_.draws == UINT64_MAX)
        {
            Fail(FailureCode::ContextUnavailable);
            return forward();
        }
        image_.combat_state = NextCrt(image_.combat_state);
        ++image_.draws;
        return static_cast<int>((image_.combat_state >> 16) & 0x7fff);
    }
    if (warmup)
    {
        std::uint32_t before{};
        if (!image_.seeded || image_.combat_ready
            || image_.warmup_draws >= ((image_.seed_state & 0xffu) << 4) + 15
            || !ReadNativeState(before) || before != image_.state)
        {
            Fail(FailureCode::RestorePreflightFailed);
            return forward();
        }
        const int result = forward();
        if (!original || !ReadNativeState(image_.state)
            || image_.state != NextCrt(before)
            || result != static_cast<int>((image_.state >> 16) & 0x7fff))
            Fail(FailureCode::RestoreVerificationFailed);
        else ++image_.warmup_draws;
        return result;
    }
    if (!image_.combat_ready || image_.native_draws == UINT64_MAX)
    {
        Fail(FailureCode::RestorePreflightFailed);
        return forward();
    }
    const int result = forward();
    if (!original) Fail(FailureCode::ContextUnavailable);
    ++image_.native_draws;
    if (!IsNativePresentation(return_rva)) ++image_.unknown_draws;
    return result;
}

void UcrtRandBroker::HandleSrand(std::uint32_t thread_id,
    std::uintptr_t return_rva, unsigned int seed, UcrtSrandFn original) noexcept
{
    if (original != nullptr) original(seed);
    if (mode_ == UcrtRandBrokerMode::Disabled
        || mode_ == UcrtRandBrokerMode::Failed)
    {
        return;
    }
    if (return_rva != Schema::Sc6UcrtLayout::rng_init_srand_return_rva)
    {
        if (IsOwner(thread_id)) Fail(FailureCode::RestorePreflightFailed);
        return;
    }
    std::uint32_t unowned{};
    owner_thread_id_.compare_exchange_strong(unowned, thread_id);
    if (!RequireOwner(thread_id)) return;
    if (!original || seed > 0x0fffffffu || next_epoch_ == UINT64_MAX
        || (image_.seeded && !image_.combat_ready))
    {
        Fail(FailureCode::RestorePreflightFailed);
        return;
    }
    image_ = {};
    image_.algorithm_version = Schema::Sc6UcrtLayout::algorithm_version;
    image_.allowlist_version = Schema::Sc6UcrtLayout::allowlist_version;
    image_.seed_state = seed;
    image_.epoch = ++next_epoch_;
    if (!ReadNativeState(image_.state) || image_.state != seed)
    {
        Fail(FailureCode::RestoreVerificationFailed);
        return;
    }
    image_.seeded = true;
    // Old epoch loses restore permission. Never overwrite a concurrently
    // reported foreign-thread failure with a successful initialization.
    auto expected = UcrtRandBrokerMode::Owned;
    mode_.compare_exchange_strong(expected, UcrtRandBrokerMode::Observing);
}

Status UcrtRandBroker::Capture(
    std::uint32_t thread_id, UcrtRandBrokerImage& output) noexcept
{
    output = {};
    if (mode_ == UcrtRandBrokerMode::Disabled
        || mode_ == UcrtRandBrokerMode::Failed)
    {
        return Status::failure(failure() == FailureCode::None
            ? FailureCode::ContextUnavailable : failure());
    }
    if (!IsOwner(thread_id)) return Status::failure(FailureCode::WrongThread);
    if (!ValidateImage(image_)) return Status::failure(FailureCode::ContextUnavailable);
    if (!ReadNativeState(image_.state)) return Status::failure(FailureCode::ContextUnavailable);
    output = image_;
    return Status::success();
}

Status UcrtRandBroker::Restore(
    std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept
{
    const auto ready = PreflightRestore(thread_id, image);
    if (!ready.ok()) return ready; // Invalid A never poisons complete B.
    std::uint32_t current{};
    if (!ReadNativeState(current)) return Status::failure(FailureCode::ContextUnavailable);
    const auto prior = current;
    native_seed_(image.state);
    if (!ReadNativeState(current) || current != image.state)
    {
        native_seed_(prior);
        if (!ReadNativeState(current) || current != prior)
        {
            Fail(FailureCode::UndoFailed);
            return Status::failure(failure_);
        }
        return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    // Only now commit private state/counters. No callbacks or allocations
    // occur between the verified native write and this owner-thread commit.
    image_ = image;
    return Status::success();
}

bool UcrtRandBroker::ValidateImage(const UcrtRandBrokerImage& image) noexcept
{
    return image.algorithm_version == Schema::Sc6UcrtLayout::algorithm_version
        && image.allowlist_version == Schema::Sc6UcrtLayout::allowlist_version
        && image.seeded && image.combat_ready && image.epoch != 0
        && ValidWarmup(image) && image.unknown_draws <= image.native_draws;
}

Status UcrtRandBroker::PreflightRestore(
    std::uint32_t thread_id, const UcrtRandBrokerImage& image) noexcept
{
    if (!IsOwner(thread_id)) return Status::failure(FailureCode::WrongThread);
    if (mode_ != UcrtRandBrokerMode::Owned)
        return Status::failure(FailureCode::IllegalTransition);
    if (!ValidateImage(image) || !ValidateImage(image_)
        || image.epoch != image_.epoch || image.seed_state != image_.seed_state
        || image.warmup_draws != image_.warmup_draws)
        return Status::failure(FailureCode::RestorePreflightFailed);
    std::uint32_t ignored{};
    return ReadNativeState(ignored) ? Status::success()
        : Status::failure(FailureCode::ContextUnavailable);
}

const char* UcrtRandBroker::Lane(std::uint32_t thread_id, std::uintptr_t rva) const noexcept
{
    if (!IsOwner(thread_id)) return "native_foreign";
    if (mode_ == UcrtRandBrokerMode::Disabled || mode_ == UcrtRandBrokerMode::Failed)
        return "native_unadmitted";
    if (rva == Schema::Sc6UcrtLayout::movevm_rand_return_rva)
        return image_.combat_ready ? "combat_private" : "native_unadmitted";
    if (rva == Schema::Sc6UcrtLayout::rng_init_rand_return_rva)
        return "native_warmup";
    return IsNativePresentation(rva) ? "native_presentation" : "native_unresolved";
}
}
