#include "../HorseMod/horselib/RollbackFloatingPointEnvironment.hpp"

#include <Windows.h>
#include <float.h>
#include <immintrin.h>

#include <cstdio>

int main()
{
    const uint32_t original_mxcsr = _mm_getcsr();
    const uint32_t original_x87 =
        static_cast<uint32_t>(_control87(0, 0));

    _mm_setcsr((original_mxcsr & ~_MM_ROUND_MASK) | _MM_ROUND_UP);
    (void)_control87(_RC_DOWN, _MCW_RC);
    const uint32_t perturbed_mxcsr = _mm_getcsr();
    const uint32_t perturbed_x87 =
        static_cast<uint32_t>(_control87(0, 0));

    bool restored = false;
    {
        Horse::RollbackFloatingPointEnvironmentScope scope(
            GetCurrentThreadId());
        if (!scope || !Horse::RollbackFloatingPointPolicyInstalled())
        {
            std::printf("floating-point policy installation failed\n");
            return 1;
        }
        {
            Horse::RollbackFloatingPointEnvironmentScope nested(
                GetCurrentThreadId());
            if (nested)
            {
                std::printf("nested floating-point scope was admitted\n");
                return 1;
            }
        }
        Horse::RollbackFloatingPointEnvironmentScope nested_after_destroy(
            GetCurrentThreadId());
        if (nested_after_destroy)
        {
            std::printf("destroyed rejected scope released outer guard\n");
            return 1;
        }
        _mm_setcsr(_mm_getcsr() | _MM_EXCEPT_INEXACT);
        if (!Horse::RollbackFloatingPointPolicyInstalled())
        {
            std::printf("MXCSR status was mistaken for control drift\n");
            return 1;
        }
        restored = scope.restore();
    }
    if (!restored || _mm_getcsr() != perturbed_mxcsr
        || (static_cast<uint32_t>(_control87(0, 0)) & _MCW_RC)
            != (perturbed_x87 & _MCW_RC))
    {
        std::printf("floating-point caller environment restore failed\n");
        return 1;
    }

    bool control_drift_rejected = false;
    {
        Horse::RollbackFloatingPointEnvironmentScope scope(
            GetCurrentThreadId());
        if (!scope)
        {
            std::printf("second floating-point policy installation failed\n");
            return 1;
        }
        _mm_setcsr(
            (_mm_getcsr() & ~_MM_ROUND_MASK) | _MM_ROUND_UP);
        control_drift_rejected = !scope.restore();
    }
    if (!control_drift_rejected || _mm_getcsr() != perturbed_mxcsr
        || (static_cast<uint32_t>(_control87(0, 0)) & _MCW_RC)
            != (perturbed_x87 & _MCW_RC))
    {
        std::printf("floating-point control drift was not fail-closed\n");
        return 1;
    }

    (void)_control87(original_x87, Horse::kRollbackFloatingPointX87ControlMask);
    _mm_setcsr(original_mxcsr);
    std::printf("rollback floating-point environment self-test passed\n");
    return 0;
}
