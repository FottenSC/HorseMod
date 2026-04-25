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
// renderer to notice.  `ULineBatchComponent::MarkRenderStateDirty` is not
// exposed as a UFunction in Shipping and the C++ method symbol isn't
// exported (and may be inlined).  We rely on the component's own Tick:
//
//   1. Our mod runs each frame in the CockpitBase_C::Update pre-hook.
//   2. It appends each line with RemainingLifeTime = kLifetime (~0.1 s).
//   3. A few frames later TickComponent's lifetime sweep marks an entry
//      as expired, removes it, and calls MarkRenderStateDirty as a side
//      effect.  The scene proxy then rebuilds from the current array,
//      which includes all our newly-appended lines.
//
// Steady state: every frame we append, every frame some tail entries
// expire, so the proxy is constantly being refreshed — exactly what we
// want for a per-frame debug overlay.  Visible lag is ~kLifetime seconds
// on toggle-off (lines finish their lifetime and fade out naturally
// instead of needing an explicit clear).
// ============================================================================

#pragma once

#include "HorseLib.hpp"

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
        // RemainingLifeTime for every appended line.  A few frames is enough
        // for a tail entry to expire and trigger the scene-proxy rebuild.
        // Too short and we flicker (proxy rebuilds faster than we re-append);
        // too long and toggle-off takes visibly longer to clear.
        static constexpr float kLifetime = 0.10f;   // seconds

        // Initial reservation so we don't call (*GMalloc)->Realloc every
        // frame just to grow.  64 lines = 2 charas × ~32 segments — plenty
        // of headroom for AABBs + per-move capsules later.
        static constexpr int32_t kInitialCapacity = 64;

        // Change which of UWorld's three batchers we append to.  Takes
        // effect on the next primeFrom (or immediately if the UWorld
        // pointer hasn't moved — simplest: call invalidate() after).
        void setSlot(LineBatcherSlot slot) { m_slot = slot; invalidate(); }
        LineBatcherSlot slot() const       { return m_slot; }

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
            slot->RemainingLifeTime = kLifetime;
            slot->DepthPriority     = 0;   // SDPG_World — depth-tested
            slot->_pad[0] = slot->_pad[1] = slot->_pad[2] = 0;
            arr->Num += 1;
        }

        void endFrame() override
        {
            // Nothing — the component's own TickComponent picks up the
            // appends and will mark its render state dirty on the next
            // lifetime sweep (see plate comment).
        }

        void hideAll() override
        {
            // Appended lines expire naturally after kLifetime seconds.
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
    };

} // namespace Horse
