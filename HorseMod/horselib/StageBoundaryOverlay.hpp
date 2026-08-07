// ============================================================================
// Horse::StageBoundaryOverlay
//
// Draws the live stage data used by LuxBattle's deterministic terrain, edge,
// and wall queries, plus state-selected presentation bounds for breakable
// wall/barrier actors. The deterministic triangles come from the active
// J_StgHitChkData-backed frame-bounds grid. The unrelated 0x144844070 block is
// a round-restore payload, not stage geometry.
//
// Ghidra anchors:
//   LuxBattle_GetActiveFrameBoundsGrid        @ image+0x3133E0
//   LuxBattle_GetActiveFrameTransform         @ image+0x313400
//   LuxBattle_TestFrameBoundsCell             @ image+0x3916E0
//   LuxBattle_IntersectSegmentWithTerrainTriangle @ image+0x390A90
//   g_LuxBattle_FrameContextUseB              @ image+0x470DEDC
//   g_LuxBattle_FrameTransformA               @ image+0x4844170
//   g_LuxBattle_FrameTransformB               @ image+0x4845220
//   g_LuxBattle_FrameBoundsGridA              @ image+0x4844DD0
//   g_LuxBattle_FrameBoundsGridB              @ image+0x4845E80
//
// Frame-bounds grid summary (LuxBattle_AttachStgHitChkData @ image+0x392080):
//   grid+0x000  J_StgHitChkData header/axis-span pointer
//   grid+0x408  contiguous FLuxTerrainHitInfo_Partial array pointer
//   grid+0x410  live byte
//   header+0x2C signed terrain-entry count
//   terrain entries are exactly 0x40 bytes each
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "LineBatcherBackend.hpp"
#include "NativeBinding.hpp"
#include "SafeMemoryRead.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Horse
{
    class StageBoundaryOverlay final
    {
    public:
        int draw(LineBatcherBackend& backend, Obj stageActorManager = {})
        {
            DrawStats stats{};
            stats.terrain = drawFrameTerrainEntries(backend, stats);
            refreshActorCache(stageActorManager);
            stats.breakableWalls = drawBreakableWalls(backend);
            stats.breakableBarriers = drawBreakableBarriers(backend);

            logStatsRateLimited(stats);
            return stats.totalLines();
        }

        void invalidate()
        {
            m_cachedManager = nullptr;
            m_breakableWallActors.clear();
            m_barrierActors.clear();
            m_actorCacheValid = false;
            m_lastActorCacheRebuild = Clock::time_point{};
            m_boundsFns.invalidate();
        }

    private:
        using Clock = std::chrono::steady_clock;

        static constexpr uintptr_t kFrameContextUseBRVA = 0x470DEDC;
        static constexpr uintptr_t kFrameTransformARVA = 0x4844170;
        static constexpr uintptr_t kFrameTransformBRVA = 0x4845220;
        static constexpr uintptr_t kFrameBoundsGridARVA = 0x4844DD0;
        static constexpr uintptr_t kFrameBoundsGridBRVA = 0x4845E80;

        static constexpr uintptr_t kGridAxisSpanPtr    = 0x000;
        static constexpr uintptr_t kGridTerrainEntries = 0x408;
        static constexpr uintptr_t kGridValid          = 0x410;
        static constexpr uintptr_t kAxisSpanTerrainCount = 0x02C;

        static constexpr uintptr_t kTriFlagsPrimary    = 0x00C;
        static constexpr uintptr_t kTriFlagsSecondary  = 0x01C;
        static constexpr uintptr_t kTerrainEntryStride = 0x040;
        static constexpr uintptr_t kFrameTransformTerrainBase = 0x820;
        static constexpr uintptr_t kFrameTransformStride = 0x10;

        // ALuxStageBreakableWallActor / BarrierActor fields proven by the
        // native visibility functions and rollback-stage capture path.
        static constexpr uintptr_t kWallBreakState = 0x468;
        static constexpr uintptr_t kWallFadeTimer = 0x46C;
        static constexpr uintptr_t kWallFadeRate = 0x470;
        static constexpr uintptr_t kBarrierEndurance = 0x424;
        static constexpr uintptr_t kBarrierHitCount = 0x468;

        static constexpr std::chrono::milliseconds kActorBoundsCacheRefresh{1000};

        static constexpr float kBattleToUE = 100.0f;
        static constexpr float kTerrainThickness = 1.5f;
        static constexpr float kActorThickness = 2.0f;
        static constexpr float kMaxAbsBattleCoordinate = 100000.0f;
        static constexpr float kMaxAbsUeCoordinate = 10000000.0f;

        static constexpr FLinColor kTerrainColour{0.10f, 0.85f, 1.00f, 0.55f};
        static constexpr FLinColor kRingWallColour{1.00f, 0.72f, 0.12f, 0.90f};
        static constexpr FLinColor kFloorCeilingColour{0.35f, 0.65f, 1.00f, 0.80f};
        static constexpr FLinColor kEdgeRingOutColour{0.10f, 1.00f, 1.00f, 0.95f};
        static constexpr FLinColor kSpecialTerrainColour{0.75f, 0.25f, 1.00f, 0.90f};
        static constexpr FLinColor kExcludedTerrainColour{0.55f, 0.55f, 0.55f, 0.65f};
        static constexpr FLinColor kBreakableWallColour{1.00f, 0.10f, 0.55f, 0.90f};
        static constexpr FLinColor kBreakableBarrierColour{0.20f, 1.00f, 0.35f, 0.90f};

        struct DrawStats
        {
            int terrain = 0;
            int ordinaryTerrainTriangles = 0;
            int ringWallTriangles = 0;
            int floorCeilingTriangles = 0;
            int edgeRingOutTriangles = 0;
            int specialTerrainTriangles = 0;
            int excludedTerrainTriangles = 0;
            int invalidFrameTriangles = 0;
            int breakableWalls = 0;
            int breakableBarriers = 0;
            bool gridValid = false;

            int totalLines() const
            {
                return terrain + breakableWalls + breakableBarriers;
            }
        };

        struct Bounds
        {
            FVec3 origin{};
            FVec3 extent{};
        };

        struct FrameContext
        {
            const uint8_t* grid = nullptr;
            const uint8_t* transform = nullptr;
        };

        struct KismetBoundsFns
        {
            RC::Unreal::UObject* cdo = nullptr;
            RC::Unreal::UFunction* getComponentBounds = nullptr;

            bool resolve()
            {
                using namespace RC;
                using namespace RC::Unreal;

                if (cdo && getComponentBounds)
                    return true;

                cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr,
                    STR("/Script/Engine.Default__KismetSystemLibrary"));
                getComponentBounds = cdo
                    ? cdo->GetFunctionByNameInChain(L"GetComponentBounds")
                    : nullptr;
                return cdo && getComponentBounds;
            }

            void invalidate()
            {
                cdo = nullptr;
                getComponentBounds = nullptr;
            }
        };

        RC::Unreal::UObject* m_cachedManager = nullptr;
        std::vector<RC::Unreal::UObject*> m_breakableWallActors;
        std::vector<RC::Unreal::UObject*> m_barrierActors;
        bool m_actorCacheValid = false;
        Clock::time_point m_lastActorCacheRebuild{};
        KismetBoundsFns m_boundsFns{};

        static bool sane(float v, float maxAbs)
        {
            return std::isfinite(v) && std::fabs(v) < maxAbs;
        }

        static bool saneBattlePoint(const FVec3& p)
        {
            return sane(p.X, kMaxAbsBattleCoordinate) &&
                   sane(p.Y, kMaxAbsBattleCoordinate) &&
                   sane(p.Z, kMaxAbsBattleCoordinate);
        }

        static bool saneUePoint(const FVec3& p)
        {
            return sane(p.X, kMaxAbsUeCoordinate) &&
                   sane(p.Y, kMaxAbsUeCoordinate) &&
                   sane(p.Z, kMaxAbsUeCoordinate);
        }

        static FVec3 battleWorldToUE(const FVec3& p)
        {
            // Stage terrain entries are already in battle world X/Y/Z with
            // Y vertical. Native terrain queries sample the horizontal XZ
            // plane, so map Lux Y to UE's vertical Z axis here.
            return FVec3{
                p.X * kBattleToUE,
                p.Z * kBattleToUE,
                p.Y * kBattleToUE
            };
        }

        static bool readVec3(const uint8_t* addr, FVec3& out)
        {
            return SafeReadFloat(addr + 0x0, &out.X) &&
                   SafeReadFloat(addr + 0x4, &out.Y) &&
                   SafeReadFloat(addr + 0x8, &out.Z);
        }

        static FrameContext activeFrameContext()
        {
            FrameContext ctx{};
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return ctx;

            uint8_t useB = 0;
            if (!SafeReadUInt8(reinterpret_cast<const void*>(
                                   base + kFrameContextUseBRVA),
                               &useB))
                return ctx;

            ctx.grid = reinterpret_cast<const uint8_t*>(
                base + (useB ? kFrameBoundsGridBRVA : kFrameBoundsGridARVA));
            ctx.transform = reinterpret_cast<const uint8_t*>(
                base + (useB ? kFrameTransformBRVA : kFrameTransformARVA));
            return ctx;
        }

        static int drawFrameTerrainEntries(LineBatcherBackend& backend,
                                           DrawStats& stats)
        {
            const FrameContext frame = activeFrameContext();
            const uint8_t* grid = frame.grid;
            if (!grid) return 0;

            uint8_t valid = 0;
            void* axisSpan = nullptr;
            if (!SafeReadUInt8(grid + kGridValid, &valid) || valid == 0)
                return 0;
            if (!SafeReadPtr(grid + kGridAxisSpanPtr, &axisSpan) || !axisSpan)
                return 0;
            stats.gridValid = true;

            int16_t terrainCount = 0;
            void* terrainEntries = nullptr;
            if (!SafeReadInt16(static_cast<const uint8_t*>(axisSpan) +
                                   kAxisSpanTerrainCount,
                               &terrainCount))
                return 0;
            if (terrainCount <= 0)
                return 0;
            if (!SafeReadPtr(grid + kGridTerrainEntries, &terrainEntries) ||
                !terrainEntries)
                return 0;

            int drawn = 0;
            const auto* entries = static_cast<const uint8_t*>(terrainEntries);
            for (int i = 0; i < terrainCount; ++i)
            {
                drawn += drawTerrainTriangle(
                    entries + static_cast<uintptr_t>(i) * kTerrainEntryStride,
                    frame, backend, stats);
            }
            return drawn;
        }

        static uint32_t terrainSemantic(uint32_t primaryFlags,
                                        uint32_t secondaryFlags)
        {
            const uint32_t subKind = (secondaryFlags >> 8) & 0xF;
            if (subKind == 1) return 0x3A;
            if (subKind == 3) return 0x3C;
            return (primaryFlags >> 4) & 0xFF;
        }

        static uint32_t terrainFrameTag(uint32_t secondaryFlags)
        {
            return (secondaryFlags >> 12) & 0xF;
        }

        static bool readFrameTerrainOffset(const FrameContext& frame,
                                           uint32_t frameTag,
                                           FVec3& out)
        {
            out = {};
            if (frameTag == 0)
                return true;
            if (frameTag > 2 || !frame.transform)
                return false;

            const uintptr_t offset =
                kFrameTransformTerrainBase +
                static_cast<uintptr_t>(frameTag - 1) * kFrameTransformStride;
            return readVec3(frame.transform + offset, out) &&
                   saneBattlePoint(out);
        }

        static FVec3 addFrameOffset(const FVec3& p, const FVec3& offset)
        {
            return FVec3{p.X + offset.X, p.Y + offset.Y, p.Z + offset.Z};
        }

        static const FLinColor& colourForTerrain(uint32_t semantic,
                                                 uint32_t secondaryFlags,
                                                 DrawStats& stats)
        {
            // Tag-word subkind 4 is rejected by segment/wall tracing but is
            // intentionally consumed by the point-sampling query matrix.
            if (((secondaryFlags >> 8) & 0xF) == 4)
            {
                ++stats.specialTerrainTriangles;
                return kSpecialTerrainColour;
            }
            if (semantic == 0x3A)
            {
                ++stats.ringWallTriangles;
                return kRingWallColour;
            }
            if (semantic == 0x3B)
            {
                ++stats.floorCeilingTriangles;
                return kFloorCeilingColour;
            }
            if (semantic == 0x3C)
            {
                ++stats.edgeRingOutTriangles;
                return kEdgeRingOutColour;
            }
            if (semantic == 0x3F)
            {
                ++stats.excludedTerrainTriangles;
                return kExcludedTerrainColour;
            }
            ++stats.ordinaryTerrainTriangles;
            return kTerrainColour;
        }

        static int drawTerrainTriangle(const uint8_t* tri,
                                       const FrameContext& frame,
                                       LineBatcherBackend& backend,
                                       DrawStats& stats)
        {
            uint32_t primaryFlags = 0;
            uint32_t secondaryFlags = 0;
            if (!SafeReadUInt32(tri + kTriFlagsPrimary, &primaryFlags))
                return 0;
            if (!SafeReadUInt32(tri + kTriFlagsSecondary, &secondaryFlags))
                return 0;
            FVec3 a{}, b{}, c{};
            if (!readVec3(tri + 0x00, a)) return 0;
            if (!readVec3(tri + 0x10, b)) return 0;
            if (!readVec3(tri + 0x20, c)) return 0;
            if (!saneBattlePoint(a) || !saneBattlePoint(b) ||
                !saneBattlePoint(c))
                return 0;

            FVec3 frameOffset{};
            if (!readFrameTerrainOffset(frame,
                                        terrainFrameTag(secondaryFlags),
                                        frameOffset))
            {
                ++stats.invalidFrameTriangles;
                return 0;
            }

            const FVec3 wa = addFrameOffset(a, frameOffset);
            const FVec3 wb = addFrameOffset(b, frameOffset);
            const FVec3 wc = addFrameOffset(c, frameOffset);
            if (!saneBattlePoint(wa) || !saneBattlePoint(wb) ||
                !saneBattlePoint(wc))
                return 0;

            const FLinColor& colour =
                colourForTerrain(terrainSemantic(primaryFlags,
                                                  secondaryFlags),
                                 secondaryFlags, stats);
            const FVec3 ua = battleWorldToUE(wa);
            const FVec3 ub = battleWorldToUE(wb);
            const FVec3 uc = battleWorldToUE(wc);
            if (!saneUePoint(ua) || !saneUePoint(ub) || !saneUePoint(uc))
                return 0;

            backend.drawLine(ua, ub, colour, kTerrainThickness);
            backend.drawLine(ub, uc, colour, kTerrainThickness);
            backend.drawLine(uc, ua, colour, kTerrainThickness);
            return 3;
        }

        bool getComponentBounds(Obj component, Bounds& out)
        {
            if (!component) return false;

            using namespace RC::Unreal;
            if (!m_boundsFns.resolve())
                return false;

            struct Params
            {
                UObject* Component;
                FVec3 Origin;
                FVec3 BoxExtent;
                float SphereRadius;
            } p{};
            p.Component = component.raw();
            m_boundsFns.cdo->ProcessEvent(m_boundsFns.getComponentBounds, &p);

            if (!saneUePoint(p.Origin) || !saneUePoint(p.BoxExtent))
                return false;
            if (p.BoxExtent.X < 0.0f || p.BoxExtent.Y < 0.0f ||
                p.BoxExtent.Z < 0.0f)
                return false;
            if (p.BoxExtent.X == 0.0f && p.BoxExtent.Y == 0.0f &&
                p.BoxExtent.Z == 0.0f)
                return false;

            out.origin = p.Origin;
            out.extent = p.BoxExtent;
            return true;
        }

        static bool isReal(RC::Unreal::UObject* obj)
        {
            return obj && RC::Unreal::UObject::IsReal(obj);
        }

        static void addUnique(std::vector<RC::Unreal::UObject*>& v,
                              RC::Unreal::UObject* obj)
        {
            if (!isReal(obj))
                return;
            for (auto* existing : v)
            {
                if (existing == obj)
                    return;
            }
            v.push_back(obj);
        }

        void refreshActorCache(Obj mgr)
        {
            auto* rawManager = mgr.raw();
            if (!rawManager)
            {
                m_cachedManager = nullptr;
                m_breakableWallActors.clear();
                m_barrierActors.clear();
                m_actorCacheValid = false;
                m_lastActorCacheRebuild = Clock::time_point{};
                return;
            }

            const auto now = Clock::now();
            const bool dueRefresh =
                m_lastActorCacheRebuild.time_since_epoch().count() == 0 ||
                now - m_lastActorCacheRebuild >= kActorBoundsCacheRefresh;
            if (rawManager == m_cachedManager && m_actorCacheValid &&
                !dueRefresh)
                return;

            if (!isReal(rawManager))
            {
                m_cachedManager = nullptr;
                m_breakableWallActors.clear();
                m_barrierActors.clear();
                m_actorCacheValid = false;
                m_lastActorCacheRebuild = Clock::time_point{};
                return;
            }

            m_cachedManager = rawManager;
            m_breakableWallActors.clear();
            m_barrierActors.clear();
            addActorList(mgr, L"BreakableWallActorList",
                         m_breakableWallActors);
            addActorList(mgr, L"BarrierActorList", m_barrierActors);
            m_actorCacheValid = true;
            m_lastActorCacheRebuild = now;
        }

        void addActorList(Obj mgr,
                          const wchar_t* propertyName,
                          std::vector<RC::Unreal::UObject*>& out)
        {
            const TArrHdr* arr = mgr.getPtr<TArrHdr>(propertyName);
            if (!arr || !arr->Data || arr->Num <= 0 || arr->Max < arr->Num)
                return;

            auto** actors = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < arr->Num; ++i)
            {
                addUnique(out, actors[i]);
            }
        }

        int drawWallComponentPair(Obj actor,
                                  const wchar_t* opaqueName,
                                  const wchar_t* translucentName,
                                  bool opaqueVisible,
                                  LineBatcherBackend& backend)
        {
            const wchar_t* selected = opaqueVisible ? opaqueName : translucentName;
            const wchar_t* fallback = opaqueVisible ? translucentName : opaqueName;
            Obj component = actor.getObj(selected);
            if (!component)
                component = actor.getObj(fallback);
            Bounds bounds{};
            if (!component || !getComponentBounds(component, bounds))
                return 0;
            return drawAabb(backend, bounds.origin, bounds.extent,
                            kBreakableWallColour, kActorThickness);
        }

        int drawBreakableWalls(LineBatcherBackend& backend)
        {
            int drawn = 0;
            for (auto* rawActor : m_breakableWallActors)
            {
                if (!isReal(rawActor)) continue;
                const auto* actorBytes =
                    reinterpret_cast<const uint8_t*>(rawActor);
                uint8_t breakState = 0;
                float fadeTimer = 0.0f;
                float fadeRate = 0.0f;
                if (!SafeReadUInt8(actorBytes + kWallBreakState, &breakState) ||
                    !SafeReadFloat(actorBytes + kWallFadeTimer, &fadeTimer) ||
                    !SafeReadFloat(actorBytes + kWallFadeRate, &fadeRate) ||
                    !sane(fadeTimer, kMaxAbsBattleCoordinate) ||
                    !sane(fadeRate, kMaxAbsBattleCoordinate))
                    continue;

                const wchar_t* opaqueName = nullptr;
                const wchar_t* translucentName = nullptr;
                switch (breakState)
                {
                    case 0:
                        opaqueName = L"BaseMeshComponent";
                        translucentName = L"BaseTranslucentMeshComponent";
                        break;
                    case 1:
                        opaqueName = L"BreakingMeshComponent";
                        translucentName = L"BreakingTranslucentMeshComponent";
                        break;
                    case 2:
                        opaqueName = L"BrokenMeshComponent";
                        translucentName = L"BrokenTranslucentMeshComponent";
                        break;
                    default:
                        continue;
                }

                const bool opaqueVisible = fadeRate == 0.0f && fadeTimer >= 1.0f;
                drawn += drawWallComponentPair(
                    Obj{rawActor}, opaqueName, translucentName,
                    opaqueVisible, backend);
            }
            return drawn;
        }

        int drawNamedComponentBounds(Obj actor,
                                     const wchar_t* componentName,
                                     LineBatcherBackend& backend,
                                     const FLinColor& colour)
        {
            Obj component = actor.getObj(componentName);
            Bounds bounds{};
            if (!component || !getComponentBounds(component, bounds))
                return 0;
            return drawAabb(backend, bounds.origin, bounds.extent,
                            colour, kActorThickness);
        }

        int drawBreakableBarriers(LineBatcherBackend& backend)
        {
            int drawn = 0;
            for (auto* rawActor : m_barrierActors)
            {
                if (!isReal(rawActor)) continue;
                const auto* actorBytes =
                    reinterpret_cast<const uint8_t*>(rawActor);
                int32_t endurance = 0;
                int32_t hitCount = 0;
                if (!SafeReadInt32(actorBytes + kBarrierEndurance, &endurance) ||
                    !SafeReadInt32(actorBytes + kBarrierHitCount, &hitCount) ||
                    endurance <= 0 || hitCount < 0)
                    continue;

                Obj actor{rawActor};
                // BarrierFloor is not toggled by the native hit-state
                // visibility reconciler; it remains part of both states.
                drawn += drawNamedComponentBounds(
                    actor, L"BarrierFloor", backend,
                    kBreakableBarrierColour);
                if (hitCount < endurance)
                {
                    drawn += drawNamedComponentBounds(
                        actor, L"BarrierFace", backend,
                        kBreakableBarrierColour);
                    drawn += drawNamedComponentBounds(
                        actor, L"BarrierBack", backend,
                        kBreakableBarrierColour);
                }
                else
                {
                    drawn += drawNamedComponentBounds(
                        actor, L"BarrierBreaking", backend,
                        kBreakableBarrierColour);
                }
            }
            return drawn;
        }

        static int drawAabb(LineBatcherBackend& backend,
                            const FVec3& origin,
                            const FVec3& extent,
                            const FLinColor& colour,
                            float thickness)
        {
            if (!saneUePoint(origin) || !saneUePoint(extent))
                return 0;

            const FVec3 p[8] = {
                {origin.X - extent.X, origin.Y - extent.Y, origin.Z - extent.Z},
                {origin.X + extent.X, origin.Y - extent.Y, origin.Z - extent.Z},
                {origin.X + extent.X, origin.Y + extent.Y, origin.Z - extent.Z},
                {origin.X - extent.X, origin.Y + extent.Y, origin.Z - extent.Z},
                {origin.X - extent.X, origin.Y - extent.Y, origin.Z + extent.Z},
                {origin.X + extent.X, origin.Y - extent.Y, origin.Z + extent.Z},
                {origin.X + extent.X, origin.Y + extent.Y, origin.Z + extent.Z},
                {origin.X - extent.X, origin.Y + extent.Y, origin.Z + extent.Z},
            };

            static constexpr uint8_t edges[12][2] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7},
            };

            for (const auto& e : edges)
                backend.drawLine(p[e[0]], p[e[1]], colour, thickness);
            return 12;
        }

        static void logStatsRateLimited(const DrawStats& stats)
        {
            using Clock = std::chrono::steady_clock;
            static auto last = Clock::time_point{};

            const auto now = Clock::now();
            if (last.time_since_epoch().count() != 0 &&
                now - last < std::chrono::seconds(5))
                return;
            last = now;

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.StageBoundary] grid={} ordinaryTris={} ringWallTris={} "
                    "floorCeilingTris={} edgeRingOutTris={} specialTris={} excludedTris={} "
                    "invalidFrameTris={} terrainLines={} breakableWallLines={} "
                    "barrierLines={}\n"),
                stats.gridValid ? 1 : 0,
                stats.ordinaryTerrainTriangles,
                stats.ringWallTriangles,
                stats.floorCeilingTriangles,
                stats.edgeRingOutTriangles,
                stats.specialTerrainTriangles,
                stats.excludedTerrainTriangles,
                stats.invalidFrameTriangles,
                stats.terrain,
                stats.breakableWalls,
                stats.breakableBarriers);
        }
    };
}
