#pragma once

#include "Types.hpp"

#include <cstdint>

namespace Horse::Deterministic
{
enum class AuthoritativeInputDisposition : std::uint8_t
{
    Stock,
    PreparedTakeover,
    Replace,
    OwnedRoundBarrier,
    FailClosed,
};

enum class AuthoritativeInputGateAction : std::uint8_t
{
    ContinueStock,
    ContinueAuthoritative,
    AbortBeforeConsume,
};

using AuthoritativeInputPublisher = bool (*)(
    void* context, const PlayerInput (&input)[2]) noexcept;
using AuthoritativeInputCommitter = bool (*)(void* context) noexcept;

struct AuthoritativeInputGateResult
{
    AuthoritativeInputGateAction action{
        AuthoritativeInputGateAction::ContinueStock};
    PlayerInput before[2]{};
    bool before_valid{};
    bool requested{};
    bool applied{};
    bool round_barrier{};
    bool failed_closed{};
};

// This transaction is the last gate before SC6's input-filter callback and all
// later native consumers. Ownership is committed only after the complete pair
// has been published. Any publication/commit/fail-closed path orders an outer
// tick abort before a native consumer can run.
inline AuthoritativeInputGateResult ApplyAuthoritativeInputGate(
    AuthoritativeInputDisposition disposition,
    bool stock_valid, const PlayerInput (&stock)[2],
    const PlayerInput (&authoritative)[2],
    AuthoritativeInputPublisher publish, void* publish_context,
    AuthoritativeInputCommitter commit, void* commit_context) noexcept
{
    AuthoritativeInputGateResult result{};
    result.before_valid = stock_valid;
    result.before[0] = stock[0];
    result.before[1] = stock[1];
    result.requested = disposition != AuthoritativeInputDisposition::Stock;
    result.round_barrier = disposition
        == AuthoritativeInputDisposition::OwnedRoundBarrier;
    if (disposition == AuthoritativeInputDisposition::Stock)
        return result;
    if (disposition == AuthoritativeInputDisposition::FailClosed)
    {
        result.failed_closed = true;
        result.action = AuthoritativeInputGateAction::AbortBeforeConsume;
        result.before_valid = false;
        return result;
    }
    const bool prepared = disposition
        == AuthoritativeInputDisposition::PreparedTakeover;
    if (publish == nullptr
        || !publish(publish_context, authoritative)
        || (prepared && (commit == nullptr || !commit(commit_context))))
    {
        result.failed_closed = true;
        result.action = AuthoritativeInputGateAction::AbortBeforeConsume;
        result.before_valid = false;
        return result;
    }
    result.before[0] = authoritative[0];
    result.before[1] = authoritative[1];
    result.before_valid = true;
    result.applied = true;
    result.action = AuthoritativeInputGateAction::ContinueAuthoritative;
    return result;
}
}
