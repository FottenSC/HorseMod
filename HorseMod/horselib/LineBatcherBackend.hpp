// ============================================================================
// LineBatcherBackend — ILineOverlay implementation that draws real 3D lines
// via UE4's ULineBatchComponent.  No UMG / widget plumbing; no per-line
// UMG layout cost.
//
// Ghidra-verified layout (annotated in the binary for posterity)
// --------------------------------------------------------------
//   UWorld+0x40                             ULineBatchComponent* LineBatcher
//   UWorld+0x48                             ULineBatchComponent* PersistentLineBatcher
//   UWorld+0x50                             ULineBatchComponent* ForegroundLineBatcher
//
//   ULineBatchComponent (class size 0x850):
//     +0x808 TArray&lt;FBatchedLine&gt;  BatchedLines     (Data@+0x808 Num@+0x810 Max@+0x814)
//     +0x818 TArray&lt;FBatchedPoint&gt; BatchedPoints
//     +0x830 TArray&lt;FBatchedMesh&gt;  BatchedMeshes
//
//   FBatchedLine  (UScriptStruct, 0x34 bytes):
//     +0x00 FVector    Start              (3 × float)
//     +0x0C FVector    End                (3 × float)
//     +0x18 FLinearColor Color            (4 × float)
//     +0x28 float      Thickness
//     +0x2C float      RemainingLifeTime
//     +0x30 uint8      DepthPriority
//     +0x31 [3 bytes padding to 0x34]
//
// Scene-proxy dirty trigger
// -------------------------
// Appending to BatchedLines is cheap; the tricky part is getting the
// renderer to notice.  ULineBatchComponent::MarkRenderStateDirty is the
// one and only thing that recreates the scene proxy from the current
// array contents.
//
// SC6 reality (Ghidra-verified, NOT stock UE4)
// --------------------------------------------
// SC6's Shipping build has NO lifetime sweep in ULineBatchComponent.
// The class inherits UActorComponent's no-op TickComponent — the
// stock-UE4 path that decrements FBatchedLine::RemainingLifeTime and
// calls MarkRenderStateDirty when entries expire was stripped along
// with the ENABLE_DRAW_DEBUG-gated DrawDebug* UFunction bodies.  The
// +0x2C RemainingLifeTime field is allocated but never read or written
// by the engine; setting it has no effect.
//
// What actually drives proxy rebuilds:
//   * UWorld+0x40 (legacy LineBatcher) and UWorld+0x50 (Foreground) get
//     ULineBatchComponent_Flush called on them every frame from inside
//     the engine's per-frame draw fns (FUN_1417e2360 / FUN_141e4a300).
//     Flush zeros all three batch arrays AND calls MarkRenderStateDirty
//     as the last step, so the proxy is always up to date one frame
//     later.
//   * UWorld+0x48 (Persistent) is NEVER auto-flushed.  No lifetime
//     sweep, no Flush — nothing makes its proxy rebuild on its own.
//
// Implication
// -----------
//   * Foreground slot: just append, it shows up — the engine's own
//     end-of-frame Flush takes care of the dirty trigger.  Lifetime is
//     effectively per-frame regardless of what we set on the line.
//   * Persistent slot: appends silently pile up in the BatchedLines
//     array but the proxy never rebuilds.  We have to call
//     MarkRenderStateDirty ourselves, and we have to manage line
//     expiry ourselves (RemainingLifeTime is dead — the engine won't
//     remove anything for us).
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "NativeBinding.hpp"

#include <Unreal/FMemory.hpp>
#include <Unreal/World.hpp>

#include <cstdint>
#include <cstring>

namespace Horse
{
    // In-game FBatchedLine layout (verified by Ghidra against
    // Z_Construct_UScriptStruct_FBatchedLine → struct size 0x34).
    struct FBatchedLine
    {
        FVec3     Start;              // +0x00
        FVec3     End;                // +0x0C
        FLinColor Color;              // +0x18
        float     Thickness;          // +0x28
        float     RemainingLifeTime;  // +0x2C
        uint8_t   DepthPriority;      // +0x30
        uint8_t   _pad[3];            // +0x31..+0x33
    };
    static_assert(sizeof(FBatchedLine) == 0x34, "FBatchedLine must match UE4 layout");

    // Which of UWorld's two USEFUL line batchers to append to.  UWorld
    // exposes three batcher slots; the third (LineBatcher @ +0x40) is
    // depth-tested per-frame and was historically exposed as
    // LineBatcherSlot::Default but is now hidden — it produced occluded
    // hitbox / hurtbox lines that disappeared behind characters and
    // stage geometry, which defeats the entire purpose of an overlay.
    //
    // Confirmed in Ghidra at Z_Construct_UClass_UWorld:
    //   LineBatcher            @ +0x40   depth-tested, per-frame  (REMOVED)
    //   PersistentLineBatcher  @ +0x48   depth-tested, persist until flush
    //   ForegroundLineBatcher  @ +0x50   NO depth test, always-on-top
    //
    // Persistent vs Foreground:
    //   Foreground — always-on-top.  Best general default.  Hitbox /
    //                hurtbox shapes read cleanly at any camera angle.
    //   Persistent — depth-tested + lines remain alive past the kLifetime
    //                expiry.  Useful for tracing a chara's hurtbox path
    //                across a move (set hurtboxes to Persistent and the
    //                trail accumulates).  Unsuitable for hitboxes — the
    //                accumulation would be unreadable noise.
    enum class LineBatcherSlot : uint8_t
    {
        Persistent = 0,   // UWorld+0x48
        Foreground = 1,   // UWorld+0x50 — lines always drawn on top
    };

    class LineBatcherBackend final : public ILineOverlay
    {
    public:
        // Default RemainingLifeTime for every appended line.  A few render
        // frames is enough for a tail entry to expire and trigger the scene-
        // proxy rebuild — short enough that the per-frame overlay doesn't
        // smear, long enough not to flicker.  This is the value Normal-slot
        // backends use; Persistent-slot backends override it via setLifetime
        // to control the visible trail length.
        static constexpr float kDefaultLifetime = 0.10f;   // seconds

        // Initial reservation so we don't call (*GMalloc)->Realloc every
        // frame just to grow.  64 lines = 2 charas × ~32 segments — plenty
        // of headroom for AABBs + per-move capsules later.
        static constexpr int32_t kInitialCapacity = 64;

        // Change which of UWorld's three batchers we append to.  Takes
        // effect on the next primeFrom (or immediately if the UWorld
        // pointer hasn't moved — simplest: call invalidate() after).
        void setSlot(LineBatcherSlot slot) { m_slot = slot; invalidate(); }
        LineBatcherSlot slot() const       { return m_slot; }

        // Override the per-line RemainingLifeTime (seconds).  Used to
        // extend the visible trail in the Persistent slot — the dllmain
        // UI exposes a "Trail frames" slider that converts N game frames
        // to N/60 seconds and pushes here.  Clamped >= 1ms so the engine
        // tick-sweep can still expire entries (a 0-lifetime line would
        // get freed before the proxy rebuild and never display).
        void setLifetime(float seconds)
        {
            if (seconds < 0.001f) seconds = 0.001f;
            m_lifetime = seconds;
        }
        float lifetime() const { return m_lifetime; }

        void invalidate()
        {
            m_world = nullptr;
            m_lbc   = nullptr;
        }

        // Pivot — plant a reference UObject we can call GetWorld() on.
        // The cockpit (or any chara) works.  We cache the resolved UWorld
        // and its batcher pointer for subsequent frames.
        void primeFrom(Obj pivot)
        {
            if (m_lbc && RC::Unreal::UObject::IsReal(m_lbc)) return;
            if (!pivot) return;
            auto* world = pivot.raw()->GetWorld();
            if (!world) return;
            m_world = world;

            // Pick the batcher slot per m_slot.  Both are plain
            // UObject* members on UWorld; offsets from Ghidra.  Default
            // case (unknown enum value from a stale settings.cfg load)
            // falls through to Foreground — the safer of the two for
            // first-time visibility.
            uint32_t off = Horse::WorldOffsets::ForegroundLineBatcher;
            const wchar_t* slot_label = L"ForegroundLineBatcher (on top)";
            switch (m_slot)
            {
                case LineBatcherSlot::Persistent:
                    off = Horse::WorldOffsets::PersistentLineBatcher;
                    slot_label = L"PersistentLineBatcher";
                    break;
                case LineBatcherSlot::Foreground:
                    off = Horse::WorldOffsets::ForegroundLineBatcher;
                    slot_label = L"ForegroundLineBatcher (on top)";
                    break;
            }
            auto** p = reinterpret_cast<RC::Unreal::UObject**>(
                reinterpret_cast<uint8_t*>(world) + off);
            m_lbc = *p;
            if (!m_lbc) return;

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod] LineBatcherBackend primed: {} @0x{:x} lbc=0x{:x}\n"),
                slot_label,
                reinterpret_cast<uintptr_t>(world),
                reinterpret_cast<uintptr_t>(m_lbc));
        }

        bool isReady() const { return m_lbc != nullptr; }

        // ---------- ILineOverlay ----------
        void beginFrame() override
        {
            // Nothing needed — appends happen via drawLine.
        }

        void drawLine(const FVec3& a, const FVec3& b,
                      const FLinColor& color, float thickness) override
        {
            if (!isReady()) return;
            auto* arr = batchedLinesHeader();
            if (!arr) return;

            // Ensure capacity.  GMalloc::Realloc uses the same allocator the
            // component itself uses, so later TickComponent frees are safe.
            reserveAtLeast(arr, arr->Num + 1);

            auto* slot = static_cast<FBatchedLine*>(arr->Data) + arr->Num;
            slot->Start             = a;
            slot->End               = b;
            slot->Color             = color;
            slot->Thickness         = (thickness > 0.0f) ? thickness : 1.0f;
            slot->RemainingLifeTime = m_lifetime;
            slot->DepthPriority     = 0;   // SDPG_World — depth-tested
            slot->_pad[0] = slot->_pad[1] = slot->_pad[2] = 0;
            arr->Num += 1;
        }

        void endFrame() override
        {
            // SC6 stripped the lifetime sweep that would normally call
            // MarkRenderStateDirty on entry expiry, so we have to do it
            // ourselves — otherwise the proxy stays frozen at its last
            // snapshot and our newly-appended lines don't render until
            // some unrelated event happens to dirty it.  Cheap: just
            // sets a flag and queues an end-of-frame proxy recreate.
            if (m_lbc) Horse::NativeBinding::markRenderStateDirty(m_lbc);
        }

        void hideAll() override
        {
            // Appended lines expire naturally after m_lifetime seconds.
            // If you want an immediate clear, zero Num here — but you'd
            // also need to trip the dirty flag somehow, which we don't
            // have a clean handle on yet.  For a debug overlay, fade-out
            // via lifetime is fine.
        }

        RC::Unreal::UObject* linebatcher() const { return m_lbc; }
        RC::Unreal::UObject* world()       const { return m_world; }

    private:
        TArrHdr* batchedLinesHeader()
        {
            if (!m_lbc) return nullptr;
            return reinterpret_cast<TArrHdr*>(
                reinterpret_cast<uint8_t*>(m_lbc) + 0x808);
        }

        // Grow the raw TArray header using the game's allocator so later
        // frees by the component itself are valid.
        void reserveAtLeast(TArrHdr* arr, int32_t needed)
        {
            if (arr->Max >= needed) return;
            const int32_t newMax = (arr->Max == 0)
                ? kInitialCapacity
                : (needed + (needed / 4) + 16);
            using RC::Unreal::GMalloc;
            arr->Data = (*GMalloc)->Realloc(
                arr->Data,
                static_cast<size_t>(newMax) * sizeof(FBatchedLine),
                alignof(FBatchedLine));
            arr->Max = newMax;
        }

        RC::Unreal::UObject* m_world = nullptr;
        RC::Unreal::UObject* m_lbc   = nullptr;
        // Default to Foreground so hitboxes draw on top of the weapon
        // mesh — the natural choice for a debug overlay.  Flip via
        // setSlot() / the ImGui tab.
        LineBatcherSlot      m_slot  = LineBatcherSlot::Foreground;
        // Per-line RemainingLifeTime in seconds.  Persistent-slot users
        // override via setLifetime() to control trail length.
        float                m_lifetime = kDefaultLifetime;
    };

} // namespace Horse
