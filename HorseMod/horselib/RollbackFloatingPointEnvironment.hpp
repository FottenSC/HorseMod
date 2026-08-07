#pragma once

#include <Windows.h>
#include <float.h>
#include <immintrin.h>

#include <cfenv>
#include <cstdint>

namespace Horse
{
    // Versioned bilateral simulation contract. SC6's scalar SSE camera and
    // MoveVM arithmetic is sensitive to rounding/denormal controls.
    static constexpr uint32_t kRollbackFloatingPointPolicyId = 1;
    static constexpr uint32_t kRollbackFloatingPointMxcsr = 0x00001F80u;
    static constexpr uint32_t kRollbackFloatingPointX87Control = _CW_DEFAULT;
    static constexpr uint32_t kRollbackFloatingPointX87ControlMask =
        _MCW_DN | _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC;

    struct RollbackFloatingPointEnvironmentState
    {
        std::fenv_t environment {};
        uint32_t mxcsr {0};
        uint32_t x87_control {0};
        DWORD thread_id {0};
        bool valid {false};
    };

    static inline bool RollbackFloatingPointPolicyInstalled() noexcept
    {
        const uint32_t mxcsr = _mm_getcsr();
        const uint32_t x87 = static_cast<uint32_t>(_control87(0, 0));
        // MXCSR bits 0..5 are accrued exception status, not control. Native
        // scalar SSE is allowed to set them without violating the bilateral
        // rounding/denormal/exception-mask contract.
        return (mxcsr & ~0x3Fu)
                == (kRollbackFloatingPointMxcsr & ~0x3Fu)
            && (x87 & kRollbackFloatingPointX87ControlMask)
                == (kRollbackFloatingPointX87Control
                    & kRollbackFloatingPointX87ControlMask);
    }

    class RollbackFloatingPointEnvironmentScope
    {
    public:
        explicit RollbackFloatingPointEnvironmentScope(
            DWORD expected_thread_id) noexcept
        {
            const DWORD current_thread = GetCurrentThreadId();
            if (!expected_thread_id || current_thread != expected_thread_id
                || s_active)
            {
                return;
            }
            s_active = true;
            m_owns_guard = true;
            m_state.thread_id = current_thread;
            m_state.mxcsr = _mm_getcsr();
            m_state.x87_control =
                static_cast<uint32_t>(_control87(0, 0));
            if (std::fegetenv(&m_state.environment) != 0)
            {
                s_active = false;
                m_owns_guard = false;
                return;
            }
            m_state.valid = true;

            (void)std::feclearexcept(FE_ALL_EXCEPT);
            (void)_clearfp();
            (void)_control87(
                kRollbackFloatingPointX87Control,
                kRollbackFloatingPointX87ControlMask);
            _mm_setcsr(kRollbackFloatingPointMxcsr);
            m_installed = RollbackFloatingPointPolicyInstalled();
            if (!m_installed)
                (void)restore();
        }

        RollbackFloatingPointEnvironmentScope(
            const RollbackFloatingPointEnvironmentScope&) = delete;
        RollbackFloatingPointEnvironmentScope& operator=(
            const RollbackFloatingPointEnvironmentScope&) = delete;

        ~RollbackFloatingPointEnvironmentScope() noexcept
        {
            if (m_owns_guard && !m_restored) (void)restore();
        }

        explicit operator bool() const noexcept
        {
            return m_installed && !m_restored;
        }

        bool restore() noexcept
        {
            if (m_restored) return m_restore_ok;
            m_restored = true;
            if (!m_owns_guard || !m_state.valid
                || GetCurrentThreadId() != m_state.thread_id)
            {
                m_restore_ok = false;
                return false;
            }

            // A synchronous native callback may accrue floating-point status,
            // but changing the installed control contract is an owned-iteration
            // failure even though the caller environment must still be restored.
            const bool policy_unchanged =
                RollbackFloatingPointPolicyInstalled();

            // fesetenv restores x87 status/control; MXCSR is written last so
            // its saved status and control bits are exact even on CRTs whose
            // fenv_t also carries an SSE environment.
            const bool environment_ok =
                std::fesetenv(&m_state.environment) == 0;
            _mm_setcsr(m_state.mxcsr);
            const uint32_t restored_x87 =
                static_cast<uint32_t>(_control87(0, 0));
            m_restore_ok = policy_unchanged && environment_ok
                && _mm_getcsr() == m_state.mxcsr
                && (restored_x87 & kRollbackFloatingPointX87ControlMask)
                    == (m_state.x87_control
                        & kRollbackFloatingPointX87ControlMask);
            s_active = false;
            return m_restore_ok;
        }

    private:
        inline static thread_local bool s_active = false;
        RollbackFloatingPointEnvironmentState m_state {};
        bool m_owns_guard {false};
        bool m_installed {false};
        bool m_restored {false};
        bool m_restore_ok {false};
    };
}
