// ============================================================================
// Horse::RollbackCameraPresentation
//
// Small value-only adapter for native camera vibration output. The native
// updater must advance its rollback-snapshotted state on every simulation
// pass, while its external presentation output is restored until confirmation.
// ============================================================================

#pragma once

#include <cmath>
#include <cstdint>

namespace Horse
{
#pragma pack(push, 1)
    struct RollbackCameraOutputValue
    {
        float x {0.0f};
        float y {0.0f};
        float z {0.0f};
        uint32_t active {0};
    };

    enum RollbackCameraPresentationWriteMask : uint32_t
    {
        RollbackCameraWriteActive = 1u << 0,
        RollbackCameraWriteXyz = 1u << 1,
    };

    struct RollbackCameraPresentationValue
    {
        float x {0.0f};
        float y {0.0f};
        float z {0.0f};
        uint32_t active {0};
        uint32_t write_mask {RollbackCameraWriteActive};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackCameraOutputValue) == 16);
    static_assert(sizeof(RollbackCameraPresentationValue) == 20);

    constexpr bool RollbackCameraPresentationActiveValid(
        uint32_t active) noexcept
    {
        return active <= 1;
    }

    inline bool RollbackCameraPresentationValueValid(
        const RollbackCameraPresentationValue& value) noexcept
    {
        const uint32_t expected_mask = value.active == 0
            ? RollbackCameraWriteActive
            : RollbackCameraWriteActive | RollbackCameraWriteXyz;
        return RollbackCameraPresentationActiveValid(value.active)
            && value.write_mask == expected_mask
            && std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z)
            && (value.active != 0
                || (value.x == 0.0f && value.y == 0.0f
                    && value.z == 0.0f));
    }

    enum class RollbackCameraPresentationRoute : uint8_t
    {
        PassThrough,
        CaptureOnly,
        CaptureAndQueue,
    };

    constexpr RollbackCameraPresentationRoute
    SelectRollbackCameraPresentationRoute(
        bool owned_native_simulation,
        bool effects_armed,
        bool effect_frame_valid) noexcept
    {
        if (!owned_native_simulation)
            return RollbackCameraPresentationRoute::PassThrough;
        return effects_armed && effect_frame_valid
            ? RollbackCameraPresentationRoute::CaptureAndQueue
            : RollbackCameraPresentationRoute::CaptureOnly;
    }

    template<typename WriteFloatFn, typename WriteActiveFn>
    bool RollbackRestoreCameraOutput(
        const RollbackCameraOutputValue& value,
        WriteFloatFn&& write_float,
        WriteActiveFn&& write_active) noexcept
    {
        // Do not short-circuit: a failed field must not prevent attempts to
        // restore the rest of the external output.
        bool ok = true;
        ok = write_float(0, value.x) && ok;
        ok = write_float(4, value.y) && ok;
        ok = write_float(8, value.z) && ok;
        ok = write_active(value.active) && ok;
        return ok;
    }

    template<typename WriteFloatFn, typename WriteActiveFn>
    bool RollbackCommitCameraPresentation(
        const RollbackCameraPresentationValue& value,
        WriteFloatFn&& write_float,
        WriteActiveFn&& write_active) noexcept
    {
        if (!RollbackCameraPresentationValueValid(value))
            return false;
        bool ok = true;
        if ((value.write_mask & RollbackCameraWriteXyz) != 0)
        {
            ok = write_float(0, value.x) && ok;
            ok = write_float(4, value.y) && ok;
            ok = write_float(8, value.z) && ok;
        }
        ok = write_active(value.active) && ok;
        return ok;
    }

    struct RollbackCameraPresentationCaptureReport
    {
        bool initial_read {false};
        bool native_called {false};
        bool published_read {false};
        bool output_restored {false};
        bool published_valid {false};

        bool ok() const noexcept
        {
            return initial_read && native_called && published_read
                && output_restored && published_valid;
        }
    };

    template<typename ReadFn, typename NativeFn, typename WriteFn>
    RollbackCameraPresentationCaptureReport
    RollbackCaptureCameraPresentationOutput(
        ReadFn&& read,
        NativeFn&& call_native,
        WriteFn&& write,
        RollbackCameraPresentationValue& published) noexcept
    {
        RollbackCameraPresentationCaptureReport report {};
        RollbackCameraOutputValue previous {};
        RollbackCameraOutputValue native_output {};
        report.initial_read = read(previous);
        if (!report.initial_read)
            return report;

        call_native();
        report.native_called = true;
        report.published_read = read(native_output);
        // Restore the external target even when the post-call read failed.
        report.output_restored = write(previous);
        if (report.published_read)
        {
            published.active = native_output.active;
            published.write_mask = RollbackCameraWriteActive;
            if (native_output.active != 0)
            {
                published.x = native_output.x;
                published.y = native_output.y;
                published.z = native_output.z;
                published.write_mask |= RollbackCameraWriteXyz;
            }
            else
            {
                // The native inactive branch writes only the active flag.
                // Untouched external XYZ is not part of the event identity.
                published.x = 0.0f;
                published.y = 0.0f;
                published.z = 0.0f;
            }
        }
        report.published_valid = report.published_read
            && RollbackCameraPresentationValueValid(published);
        return report;
    }
}
