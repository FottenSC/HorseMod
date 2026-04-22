// ============================================================================
// HorseLib — small helper layer over UE4SS's C++ API, tuned for SoulCalibur VI.
//
// Why this exists
// ---------------
// A UE4SS C++ mod that does even moderately fancy things (read a UObject
// property, call a UFunction, reposition a UMG widget) ends up with:
//   • raw FindFirstOf("Foo") on the hot path      (O(n) over ~100k objects)
//   • GetFunctionByNameInChain(L"Bar") every call (O(class depth))
//   • a hand-rolled `struct Params { ... }` per UFunction
//   • a mutex + atomic dance for every cross-thread share
//
// All of that is boilerplate that does not depend on what your mod is
// actually trying to express.  HorseLib owns that boilerplate so the rest
// of your mod code can read like intent:
//
//   Horse::Obj chara = Horse::World::BattleManager().CharaAt(i);
//   auto pos    = chara.CallVec3("K2_GetActorLocation");
//   auto screen = Horse::Screen::Project(pos);
//
// Design principles
// -----------------
//   1. CACHE AGGRESSIVELY.  UFunction* and UObject* resolutions are
//      memoised forever (UFunction) or revalidated with UObject::IsReal
//      (UObject globals).
//   2. THIN WRAPPER, NO TYPE LEAKS.  Everything is still a RC::Unreal::UObject*
//      under the hood.  You can drop through to raw UE4SS API any time.
//   3. HEADER-ONLY.  One include, no linking dance; the mod DLL and the
//      library always agree on memory layout.
//   4. SC6-AWARE.  The `Lux::*` helpers know about the LuxBattleManager,
//      BattleCharaArray, CockpitBase_C, CollisionComponent, etc.  Other
//      games just ignore those.
//
// What's in here
// --------------
//   Horse::Obj           — Thin UObject* wrapper with typed Get/Set/Call.
//   Horse::Fn            — UFunction cache keyed by (UClass, name).
//   Horse::Screen        — World→screen projection with CDO/PC caching.
//   Horse::World         — Access to the active UWorld, its LineBatcher,
//                          and typed battle-global accessors for SC6.
//   Horse::Lux           — SC6-specific helpers (BattleManager, charas, …).
//   Horse::LineOverlay   — Abstraction for "draw N coloured lines this
//                          frame".  Backed by two implementations:
//                            * CockpitWidgetBackend (ships today, works)
//                            * LineBatcherBackend   (Phase 4 scaffold)
//
// Threading
// ---------
// All UObject access assumes the game thread.  The CockpitBase Update
// pre-hook runs on the game thread — that's where you call these helpers.
// The ImGui render callback runs on the render thread; it MUST NOT touch
// UObjects or Horse::* helpers directly.  Publish plain structs across
// the boundary (see `SnapshotLatch` pattern used in dllmain.cpp).
// ============================================================================

#pragma once

#include <Mod/CppUserModBase.hpp>
#include <UE4SSProgram.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UFunctionStructs.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace Horse
{
    // -----------------------------------------------------------------------
    // UE4.21 POD layouts.  SC6 ships UE4.21, so pre-5.0 shapes are safe.
    // -----------------------------------------------------------------------
    struct FVec3     { float X, Y, Z; };
    struct FVec2     { float X, Y; };
    struct FLinColor { float R, G, B, A; };
    struct TArrHdr   { void* Data; int32_t Num; int32_t Max; };

    // -----------------------------------------------------------------------
    // Horse::Fn — cache a UFunction* keyed by (object, function-name).
    // A single cache slot is fine when the same code path always calls the
    // same function on the same class of objects (the common case).
    // -----------------------------------------------------------------------
    class Fn
    {
    public:
        // Get-or-resolve a UFunction on `obj`.  Once resolved, the pointer
        // is stored and the next call just loads it.  UFunctions are stable
        // for their owning UClass's lifetime (i.e. forever in practice).
        RC::Unreal::UFunction* on(RC::Unreal::UObject* obj, const wchar_t* name)
        {
            if (m_cached) return m_cached;
            if (!obj) return nullptr;
            m_cached = obj->GetFunctionByNameInChain(name);
            return m_cached;
        }

        // Try multiple names in order (useful when UE4 renamed a function
        // between engine versions, e.g. GetActorLocation vs K2_GetActorLocation).
        template <class... Names>
        RC::Unreal::UFunction* onAny(RC::Unreal::UObject* obj, Names... names)
        {
            if (m_cached) return m_cached;
            if (!obj) return nullptr;
            ((m_cached = m_cached ? m_cached : obj->GetFunctionByNameInChain(names)), ...);
            return m_cached;
        }

        RC::Unreal::UFunction* raw() const { return m_cached; }
        void                   clear()     { m_cached = nullptr; }

    private:
        RC::Unreal::UFunction* m_cached = nullptr;
    };

    // -----------------------------------------------------------------------
    // Horse::GlobalPtr — FindFirstOf-backed cached singleton.  Revalidates
    // with UObject::IsReal so we recover gracefully if the engine tears
    // down and re-creates the object (e.g. level change).
    // -----------------------------------------------------------------------
    class GlobalPtr
    {
    public:
        RC::Unreal::UObject* get(const wchar_t* class_name)
        {
            if (m_ptr && RC::Unreal::UObject::IsReal(m_ptr)) return m_ptr;
            m_ptr = RC::Unreal::UObjectGlobals::FindFirstOf(class_name);
            return m_ptr;
        }
        void invalidate() { m_ptr = nullptr; }
    private:
        RC::Unreal::UObject* m_ptr = nullptr;
    };

    // -----------------------------------------------------------------------
    // Horse::Obj — thin wrapper over UObject* with typed accessors.
    // Zero overhead when inlined; degenerates to the raw UObject pointer.
    // -----------------------------------------------------------------------
    class Obj
    {
    public:
        Obj() = default;
        Obj(RC::Unreal::UObject* raw) : m_obj(raw) {}

        explicit operator bool() const { return m_obj != nullptr; }
        RC::Unreal::UObject* raw() const { return m_obj; }

        // -------- property access (POD types) --------
        template <class T>
        T* getPtr(const wchar_t* name) const
        {
            if (!m_obj) return nullptr;
            return m_obj->GetValuePtrByPropertyNameInChain<T>(name);
        }

        template <class T>
        T getValueOr(const wchar_t* name, T fallback) const
        {
            auto* p = getPtr<T>(name);
            return p ? *p : fallback;
        }

        // Read a UObject*-typed property and return it wrapped.
        Obj getObj(const wchar_t* name) const
        {
            if (!m_obj) return {};
            auto** p = m_obj->GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>(name);
            return p ? Obj{*p} : Obj{};
        }

        // -------- UFunction calls --------
        // Caller passes the cached Fn slot so repeated calls are free.
        void callRaw(Fn& fn, const wchar_t* name, void* params) const
        {
            if (!m_obj) return;
            auto* f = fn.on(m_obj, name);
            if (!f) return;
            m_obj->ProcessEvent(f, params);
        }

        // Convenience: call a UFunction that returns an FVec3 (no args).
        //  GetActorLocation / GetComponentLocation etc. fit this shape.
        FVec3 callVec3(Fn& fn, const wchar_t* name) const
        {
            struct Out { FVec3 V; } out{};
            callRaw(fn, name, &out);
            return out.V;
        }

        FVec3 callVec3Any(Fn& fn, const wchar_t* n1, const wchar_t* n2) const
        {
            if (!m_obj) return {};
            auto* f = fn.onAny(m_obj, n1, n2);
            if (!f) return {};
            struct Out { FVec3 V; } out{};
            m_obj->ProcessEvent(f, &out);
            return out.V;
        }

    private:
        RC::Unreal::UObject* m_obj = nullptr;
    };

    // -----------------------------------------------------------------------
    // Horse::Screen — camera projection helpers.
    // GameplayStatics.ProjectWorldToScreen is the canonical UE4 path and
    // stays live in Shipping builds (unlike DrawDebug*).
    // -----------------------------------------------------------------------
    class Screen
    {
    public:
        struct Result { float x, y; bool ok; };

        // Returns the screen pixel coordinates of the given world point.
        Result project(const FVec3& world)
        {
            using namespace RC;
            using namespace RC::Unreal;

            if (!m_gs_cdo || !UObject::IsReal(m_gs_cdo))
            {
                m_gs_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
                if (!m_gs_cdo) return {0, 0, false};
            }
            UObject* pc = m_pc.get(L"PlayerController");
            if (!pc) return {0, 0, false};

            auto* fn = m_fn.on(m_gs_cdo, L"ProjectWorldToScreen");
            if (!fn) return {0, 0, false};

            struct P {
                UObject*  Player;
                FVec3     WorldPos;
                FVec2     ScreenPos;
                bool      bPlayerViewportRel;
                bool      ReturnValue;
            } p{};
            p.Player   = pc;
            p.WorldPos = world;
            m_gs_cdo->ProcessEvent(fn, &p);
            return { p.ScreenPos.X, p.ScreenPos.Y, static_cast<bool>(p.ReturnValue) };
        }

        // Project the 8 corners of a world-space AABB and return the tightest
        // enclosing 2D rectangle.  Axis-agnostic — works regardless of which
        // world axis happens to be "lateral" on screen.
        struct Aabb2D { float x0, y0, x1, y1; int ok_corners; };
        Aabb2D projectAabb(const FVec3& centre, const FVec3& half)
        {
            Aabb2D r{
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                0
            };
            for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2)
            {
                FVec3 p{
                    centre.X + sx * half.X,
                    centre.Y + sy * half.Y,
                    centre.Z + sz * half.Z,
                };
                auto s = project(p);
                if (!s.ok) continue;
                r.x0 = std::min(r.x0, s.x); r.x1 = std::max(r.x1, s.x);
                r.y0 = std::min(r.y0, s.y); r.y1 = std::max(r.y1, s.y);
                ++r.ok_corners;
            }
            return r;
        }

    private:
        RC::Unreal::UObject* m_gs_cdo = nullptr;
        GlobalPtr            m_pc;
        Fn                   m_fn;
    };

    // -----------------------------------------------------------------------
    // Horse::World — helpers for reaching the active UWorld.
    // Ghidra work in this repo established:
    //   UWorld+0x40 = LineBatcher             (ULineBatchComponent*)
    //   UWorld+0x48 = PersistentLineBatcher   (ULineBatchComponent*)
    //   UWorld+0x50 = ForegroundLineBatcher   (ULineBatchComponent*)
    // Kept here so the offsets aren't scattered through mod code.
    // -----------------------------------------------------------------------
    namespace WorldOffsets
    {
        constexpr uint32_t LineBatcher            = 0x40;
        constexpr uint32_t PersistentLineBatcher  = 0x48;
        constexpr uint32_t ForegroundLineBatcher  = 0x50;
    }

    // -----------------------------------------------------------------------
    // Horse::Lux — SoulCalibur VI-specific conveniences.
    // -----------------------------------------------------------------------
    namespace LuxOffsets
    {
        // ALuxBattleChara layout (from Ghidra RE of GetTracePosition_Impl):
        constexpr uint32_t MoveProviderPtr        = 0x388; // -> ULuxBattleMoveProvider*
        // ULuxBattleMoveProvider+0x30 -> FLuxCapsuleContainer*
        constexpr uint32_t CapsuleContainerPtr    = 0x30;
        // FLuxCapsuleContainer+0x30 -> FLuxCapsule** (count at +0x38)
        constexpr uint32_t CapsuleArrayPtr        = 0x30;
        constexpr uint32_t CapsuleCount           = 0x38;
        // FLuxCapsule field offsets:
        constexpr uint32_t Capsule_Type           = 0x30;
        constexpr uint32_t Capsule_BoneA          = 0x31;
        constexpr uint32_t Capsule_OffsetA        = 0x34;
        constexpr uint32_t Capsule_BoneB          = 0x40;
        constexpr uint32_t Capsule_OffsetB        = 0x44;
    }

    class Lux
    {
    public:
        // Primary battle-state globals.  Revalidated each access.
        Obj battleManager() { return Obj{ m_bm.get(L"LuxBattleManager") }; }
        Obj cockpit()       { return Obj{ m_cockpit.get(L"CockpitBase_C") }; }

        // Returns the TArray<ALuxBattleChara*> header on LuxBattleManager.
        // Null if the battle manager isn't present yet.
        const TArrHdr* battleCharaArray()
        {
            Obj bm = battleManager();
            if (!bm) return nullptr;
            return bm.getPtr<TArrHdr>(L"BattleCharaArray");
        }

        // Yield each ALuxBattleChara* to `visitor(index, Obj)`.
        template <class Visit>
        void forEachChara(Visit&& visitor)
        {
            const TArrHdr* chars = battleCharaArray();
            if (!chars || !chars->Data || chars->Num <= 0) return;
            auto** ptrs = static_cast<RC::Unreal::UObject**>(chars->Data);
            for (int i = 0; i < chars->Num; ++i)
            {
                if (!ptrs[i]) continue;
                visitor(i, Obj{ptrs[i]});
            }
        }

        void invalidate() { m_bm.invalidate(); m_cockpit.invalidate(); }

    private:
        GlobalPtr m_bm;
        GlobalPtr m_cockpit;
    };

    // =======================================================================
    // Horse::ILineOverlay — "draw coloured line segments this frame".
    //
    // One concrete backend today: LineBatcherBackend (see
    // LineBatcherBackend.hpp) appends FBatchedLine entries to
    // UWorld->LineBatcher at +0x808, reusing the engine's existing
    // debug-line renderer.  3D world-space lines, depth-tested,
    // uncapped count, negligible per-line cost.
    //
    // The interface is kept abstract because earlier prototypes had a
    // second implementation that hijacked cockpit UMG widgets.  It was
    // deleted once LineBatcher proved out, but the seam is useful if
    // you ever port this mod to a game where the LineBatcher path
    // isn't available and need to supply your own renderer.
    // =======================================================================

    class ILineOverlay
    {
    public:
        virtual ~ILineOverlay() = default;

        // Called at the start of a frame; backend can reset per-frame state.
        virtual void beginFrame() = 0;

        // Draw a line segment from `a` to `b` in world space with `color`.
        virtual void drawLine(const FVec3& a, const FVec3& b,
                              const FLinColor& color, float thickness) = 0;

        // Called once per frame AFTER all drawLine calls.  Backends that
        // need to flush / mark dirty / commit do it here.
        virtual void endFrame() = 0;

        // Hide everything (e.g. when the overlay toggle is off).
        virtual void hideAll() = 0;
    };

} // namespace Horse
