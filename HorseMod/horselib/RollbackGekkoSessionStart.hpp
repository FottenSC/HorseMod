// ============================================================================
// Horse::RollbackGekkoSessionStart
//
// Gekko GameSession::Init clears its network adapter.  The adapter therefore
// must be installed after gekko_start(), never before it.  Production and the
// two-session acceptance harness share this helper so the tested order cannot
// drift from the live runtime.
// ============================================================================

#pragma once

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
#if HORSE_ENABLE_GEKKONET
    static inline bool StartRollbackGekkoSessionWithAdapter(
        GekkoSession* session,
        GekkoConfig& config,
        GekkoNetAdapter* adapter) noexcept
    {
        if (!session || !adapter) return false;
        gekko_start(session, &config);
        // Gekko GameSession::Init(), called by gekko_start(), resets _host.
        gekko_net_adapter_set(session, adapter);
        return true;
    }
#endif
}
