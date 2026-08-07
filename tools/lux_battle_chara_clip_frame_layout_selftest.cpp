#include "LuxBattleCharaClipFrameLayout.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace
{
    constexpr size_t kVerifiedNativeOffset = 0x2B27C;
    constexpr size_t kRejectedOldDiagnosticOffset = 0x2B47C;
    constexpr size_t kFixtureSize =
        kRejectedOldDiagnosticOffset + sizeof(float);

    void write_float(
        std::array<std::byte, kFixtureSize>& bytes,
        size_t offset,
        float value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
}

int main()
{
    static_assert(
        offsetof(
            Horse::LuxBattleCharaClipFrameLayout,
            flCurrentClipFrame) == kVerifiedNativeOffset);
    static_assert(
        Horse::kLuxBattleCharaCurrentClipFrameOffset
            == kVerifiedNativeOffset);

    std::array<std::byte, kFixtureSize> fixture {};
    write_float(fixture, kVerifiedNativeOffset, 17.25f);
    write_float(fixture, kRejectedOldDiagnosticOffset, 99.0f);

    const float captured =
        Horse::ReadLuxBattleCharaCurrentClipFrameForTest(fixture.data());
    assert(captured == 17.25f);
    assert(captured != 99.0f);

    std::cout << "lux battle chara clip-frame layout self-test passed\n";
    return 0;
}
