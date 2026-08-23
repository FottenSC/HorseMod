#pragma once

// Static-only contract for the future ImGui frame meter.  These fields are
// backed by the v2.31 Ghidra database and extracted KHD layouts, but this
// contract has intentionally not been validated against a running process.

#include <cstddef>
#include <cstdint>

namespace HorseMod::FrameMeterLiveReaderContract
{
    inline constexpr bool kRuntimeValidated = false;
    inline constexpr std::uint32_t kExecutableVersion = 0x0002'0031;

    enum class FramePhase : std::int16_t
    {
        Disabled = 0,
        Startup = 1,
        Active = 2,
        PostActive = 3,
    };

    // Native hit-classifier values. This is distinct from the generated
    // StaticLookupMode ABI and must never be used as its numeric encoding.
    enum class NativeHitClassifier : std::uint8_t
    {
        Block = 0,
        Hit = 1,
        CounterHit = 11,
        LethalHit = 12,
        PunishHit = 13,
    };

    namespace Chara
    {
        inline constexpr std::ptrdiff_t StyleId = 0x24C;               // ushort
        inline constexpr std::ptrdiff_t AirOrCinematicClass = 0x132C;// uint; >=2 contributes to air/cinematic route
        inline constexpr std::ptrdiff_t ReactionCounter = 0x1364;     // float
        inline constexpr std::ptrdiff_t ReactionDelta = 0x1370;       // float
        inline constexpr std::ptrdiff_t ReactionOrFallAggregate = 0x16DB; // byte
        inline constexpr std::ptrdiff_t Blockstun = 0x16DC;           // byte
        inline constexpr std::ptrdiff_t FramePhase = 0x1980;          // short
        inline constexpr std::ptrdiff_t OwnActiveAttackCell = 0x44058;// FLuxBattleAttackCell*
        inline constexpr std::ptrdiff_t ActiveLane = 0x44068;         // FLuxMoveLane*
        inline constexpr std::ptrdiff_t MoveBank = 0x455C0;           // FLuxMoveBank*
    }

    namespace MoveLane
    {
        inline constexpr std::ptrdiff_t PackedMove = 0x02;            // short
        inline constexpr std::ptrdiff_t CurrentAnimFrame = 0x08;      // float
        inline constexpr std::ptrdiff_t PreviousAnimFrame = 0x0C;     // float
        inline constexpr std::ptrdiff_t PlaybackSpeed = 0x30;         // float
        inline constexpr std::ptrdiff_t PlaybackSpeedTarget = 0x34;   // float
        inline constexpr std::ptrdiff_t VariantIndex = 0x460;         // uint
        inline constexpr std::size_t Size = 0x468;
    }

    namespace MoveBank
    {
        inline constexpr std::ptrdiff_t AttackCellsOffset = 0x10;     // uint, bank-relative
        inline constexpr std::size_t AttackCellSize = 0x70;
    }

    namespace AttackCell
    {
        inline constexpr std::ptrdiff_t ActiveStart = 0x36;           // short
        inline constexpr std::ptrdiff_t ActiveEnd = 0x38;             // short
        inline constexpr std::ptrdiff_t Blockstun = 0x44;             // short
        inline constexpr std::ptrdiff_t StandardGroundedCounter = 0x46;// short
        inline constexpr std::ptrdiff_t StandardAirOrCinematicCounter = 0x48;// short
        inline constexpr std::ptrdiff_t PromotedGroundedCounter = 0x4C;// short
        inline constexpr std::ptrdiff_t PromotedAirOrCinematicCounter = 0x4E;// short
        inline constexpr std::ptrdiff_t GroundedReactionRow = 0x50;   // short
        inline constexpr std::ptrdiff_t AirOrCinematicReactionRow = 0x52;// short
        inline constexpr std::size_t Size = 0x70;
    }

    // g_LuxBattle_FrameCounter increments at the tail of the native battle
    // frame. It is a history-sample label, not an animation coordinate.
    inline constexpr std::ptrdiff_t kBattleFrameCounterRva = 0x470D0C4;

    // Lookup key construction, sampled after native simulation:
    //  1. pLane = *(chara + Chara::ActiveLane), reject null.
    //  2. packed = *(short *)(pLane + MoveLane::PackedMove), reject -1.
    //  3. canonicalPacked = uint16_t(packed) & ~0x0800. Use that exact
    //     value for lookup; bankKind = canonicalPacked >> 12 and
    //     slot = canonicalPacked & 0x7FF. Bit 11 is a native alias bit.
    //  4. pCell = *(chara + Chara::OwnActiveAttackCell), reject null.
    //  5. cells = pBank + *(uint *)(pBank + MoveBank::AttackCellsOffset).
    //  6. Require pCell >= cells and an exact 0x70-byte stride; quotient is
    //     attackCellIndex. Require a generated entry for the resulting
    //     style/bank/move/cell tuple, which supplies the static upper bound.
    //     A failed range/stride/entry check suppresses lookup.
    //  7. Truncate CurrentAnimFrame toward zero for contactCoordinate.
    //  8. Derive hit classification from the accepted-contact transaction.
    //     +0x132C is not a saved Counter-Hit selector; it participates in the
    //     air/cinematic route predicate. Do not infer Hit solely from +0x16DB.
    //  9. Construct reactionContext from the actual grounded/air, promoted,
    //     guard-condition, classifier, or throw route. This is not merely a
    //     physical defender posture. No context fallback is permitted.
    // 10. Prove that +0x24C maps to the compiled KHD style id before enabling
    //     lookup, then hash-check executable, style KHD, and yarare sources.

    // State precedence for rectangle history:
    // shared freeze > accepted contact marker > defender blockstun > broader
    // reaction/fall aggregate > attacker phase. FramePhase::PostActive is
    // rendered as recovery; it never means Actionable without a proven
    // transition/control endpoint from the static table. Version 1 currently
    // publishes no numeric actionability endpoint: animation-coordinate to
    // logical-tick mapping and command-release ordering remain unclosed.
}
