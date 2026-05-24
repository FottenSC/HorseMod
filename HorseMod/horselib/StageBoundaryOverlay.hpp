// ============================================================================
// Horse::StageBoundaryOverlay
//
// Draws stage gameplay geometry: ring-out terrain, wall-collision terrain, and
// breakable wall/barrier actor bounds. The always-present ring/wall data lives
// in LuxBattle's active frame-bounds grid, not in the optional scbattle
// barrier block at g_scbattle_StageInfo_BarrierArray.
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
// Frame-bounds grid summary:
//   grid+0x00                  metadata pointer
//   grid+0x08 + cellIndex*0x08 cell bucket pointer
//   metadata+0x28              int16 cell count
//   grid+0x410                 valid byte
//
// Cell bucket summary:
//   +0x00 bucket-list A pointer, +0x08 uint16 bucket-list A count
//   +0x10 bucket-list B pointer, +0x18 uint16 bucket-list B count
//
// Bucket summary:
//   +0x00 minZ, +0x04 maxZ, +0x08 uint16 triangle count
//   +0x10 triangle pointers[]
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "LineBatcherBackend.hpp"
#include "NativeBinding.hpp"
#include "SafeMemoryRead.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
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
            stats.terrain = drawFrameBoundsGrid(backend, stats);
            stats.legacyBarrierValid = readLegacyBarrierValid();
            refreshActorBoundsCache(stageActorManager);
            stats.breakableWalls = drawCachedComponentBounds(
                m_breakableWallComponents, backend, kBreakableWallColour);
            stats.breakableBarriers = drawCachedComponentBounds(
                m_barrierComponents, backend, kBreakableBarrierColour);

            logStatsRateLimited(stats);
            return stats.totalLines();
        }

        void invalidate()
        {
            m_cachedManager = nullptr;
            m_breakableWallComponents.clear();
            m_barrierComponents.clear();
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

        static constexpr uintptr_t kLegacyBarrierValidRVA = 0x484406C;

        static constexpr uintptr_t kGridMetaPtr        = 0x000;
        static constexpr uintptr_t kGridFirstCellPtr   = 0x008;
        static constexpr uintptr_t kGridValid          = 0x410;
        static constexpr uintptr_t kGridMetaCellCount  = 0x028;

        static constexpr uintptr_t kCellListA          = 0x000;
        static constexpr uintptr_t kCellListACount     = 0x008;
        static constexpr uintptr_t kCellListB          = 0x010;
        static constexpr uintptr_t kCellListBCount     = 0x018;

        static constexpr uintptr_t kBucketTriCount     = 0x008;
        static constexpr uintptr_t kBucketFirstTriPtr  = 0x010;

        static constexpr uintptr_t kTriFlagsPrimary    = 0x00C;
        static constexpr uintptr_t kTriFlagsSecondary  = 0x01C;
        static constexpr uintptr_t kFrameTransformTerrainBase = 0x820;
        static constexpr uintptr_t kFrameTransformStride = 0x10;

        static constexpr int kMaxCells             = 4096;
        static constexpr int kMaxBucketsPerCell    = 256;
        static constexpr int kMaxTrianglesPerBucket = 512;
        static constexpr int kMaxTerrainTriangles  = 2048;
        static constexpr int kMaxActorListCount    = 64;
        static constexpr std::chrono::milliseconds kActorBoundsCacheRefresh{1000};

        static constexpr float kBattleToUE = 100.0f;
        static constexpr float kFloorLiftZ = 5.0f;
        static constexpr float kTerrainThickness = 1.5f;
        static constexpr float kActorThickness = 2.0f;
        static constexpr float kMaxAbsBattleCoordinate = 100000.0f;
        static constexpr float kMaxAbsUeCoordinate = 10000000.0f;

        static constexpr FLinColor kFloorColour{0.10f, 0.85f, 1.00f, 0.55f};
        static constexpr FLinColor kWallColour{1.00f, 0.72f, 0.12f, 0.90f};
        static constexpr FLinColor kRingOutColour{0.10f, 1.00f, 1.00f, 0.95f};
        static constexpr FLinColor kOtherBoundaryColour{0.70f, 0.95f, 0.25f, 0.80f};
        static constexpr FLinColor kBreakableWallColour{1.00f, 0.10f, 0.55f, 0.90f};
        static constexpr FLinColor kBreakableBarrierColour{0.20f, 1.00f, 0.35f, 0.90f};

        struct DrawStats
        {
            int terrain = 0;
            int floorTriangles = 0;
            int ringTriangles = 0;
            int wallTriangles = 0;
            int otherBoundaryTriangles = 0;
            int breakableWalls = 0;
            int breakableBarriers = 0;
            bool gridValid = false;
            bool legacyBarrierValid = false;
            bool terrainClipped = false;

            int totalLines() const
            {
                return terrain + breakableWalls + breakableBarriers;
            }
        };

        struct SeenTriangles
        {
            const void* items[kMaxTerrainTriangles]{};
            int count = 0;

            bool add(const void* p)
            {
                if (!p) return false;
                for (int i = 0; i < count; ++i)
                {
                    if (items[i] == p)
                        return false;
                }
                if (count >= kMaxTerrainTriangles)
                    return false;
                items[count++] = p;
                return true;
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
        std::vector<RC::Unreal::UObject*> m_breakableWallComponents;
        std::vector<RC::Unreal::UObject*> m_barrierComponents;
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
            // plane, so only lift Y into UE's vertical Z axis here.
            return FVec3{
                p.X * kBattleToUE,
                p.Z * kBattleToUE,
                p.Y * kBattleToUE + kFloorLiftZ
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

        static int drawFrameBoundsGrid(LineBatcherBackend& backend,
                                       DrawStats& stats)
        {
            const FrameContext frame = activeFrameContext();
            const uint8_t* grid = frame.grid;
            if (!grid) return 0;

            uint8_t valid = 0;
            void* meta = nullptr;
            if (!SafeReadUInt8(grid + kGridValid, &valid) || valid == 0)
                return 0;
            if (!SafeReadPtr(grid + kGridMetaPtr, &meta) || !meta)
                return 0;

            int16_t cellCount16 = 0;
            if (!SafeReadInt16(static_cast<const uint8_t*>(meta) +
                                   kGridMetaCellCount,
                               &cellCount16))
                return 0;
            if (cellCount16 <= 0 || cellCount16 > kMaxCells)
                return 0;

            stats.gridValid = true;
            int drawn = 0;
            SeenTriangles seen{};

            for (int cell = 0; cell < cellCount16; ++cell)
            {
                void* cellPtr = nullptr;
                if (!SafeReadPtr(grid + kGridFirstCellPtr +
                                     static_cast<uintptr_t>(cell) *
                                         sizeof(void*),
                                 &cellPtr) ||
                    !cellPtr)
                    continue;

                drawn += drawBucketList(static_cast<const uint8_t*>(cellPtr) +
                                            kCellListA,
                                        static_cast<const uint8_t*>(cellPtr) +
                                            kCellListACount,
                                        frame, backend, stats, seen);
                drawn += drawBucketList(static_cast<const uint8_t*>(cellPtr) +
                                            kCellListB,
                                        static_cast<const uint8_t*>(cellPtr) +
                                            kCellListBCount,
                                        frame, backend, stats, seen);

                if (seen.count >= kMaxTerrainTriangles)
                {
                    stats.terrainClipped = true;
                    break;
                }
            }

            return drawn;
        }

        static int drawBucketList(const uint8_t* listPtrAddr,
                                  const uint8_t* countAddr,
                                  const FrameContext& frame,
                                  LineBatcherBackend& backend,
                                  DrawStats& stats,
                                  SeenTriangles& seen)
        {
            void* bucketList = nullptr;
            uint16_t bucketCount = 0;
            if (!SafeReadPtr(listPtrAddr, &bucketList) || !bucketList)
                return 0;
            if (!SafeReadUInt16(countAddr, &bucketCount))
                return 0;
            if (bucketCount > kMaxBucketsPerCell)
                bucketCount = kMaxBucketsPerCell;

            int drawn = 0;
            auto* buckets = static_cast<const uint8_t*>(bucketList);
            for (uint16_t i = 0; i < bucketCount; ++i)
            {
                void* bucket = nullptr;
                if (!SafeReadPtr(buckets + static_cast<uintptr_t>(i) *
                                             sizeof(void*),
                                 &bucket) ||
                    !bucket)
                    continue;

                drawn += drawBucket(static_cast<const uint8_t*>(bucket),
                                    frame,
                                    backend, stats, seen);
                if (seen.count >= kMaxTerrainTriangles)
                {
                    stats.terrainClipped = true;
                    break;
                }
            }
            return drawn;
        }

        static int drawBucket(const uint8_t* bucket,
                              const FrameContext& frame,
                              LineBatcherBackend& backend,
                              DrawStats& stats,
                              SeenTriangles& seen)
        {
            uint16_t triCount = 0;
            if (!SafeReadUInt16(bucket + kBucketTriCount, &triCount))
                return 0;
            if (triCount > kMaxTrianglesPerBucket)
                triCount = kMaxTrianglesPerBucket;

            int drawn = 0;
            for (uint16_t i = 0; i < triCount; ++i)
            {
                if (seen.count >= kMaxTerrainTriangles)
                {
                    stats.terrainClipped = true;
                    break;
                }

                void* tri = nullptr;
                if (!SafeReadPtr(bucket + kBucketFirstTriPtr +
                                     static_cast<uintptr_t>(i) *
                                         sizeof(void*),
                                 &tri) ||
                    !tri)
                    continue;
                if (!seen.add(tri))
                    continue;

                const int lines =
                    drawTerrainTriangle(static_cast<const uint8_t*>(tri),
                                        frame,
                                        backend, stats);
                if (lines > 0)
                    drawn += lines;
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
                                                 const FVec3& a,
                                                 const FVec3& b,
                                                 const FVec3& c,
                                                 DrawStats& stats)
        {
            if (semantic == 0x3A)
            {
                ++stats.ringTriangles;
                return kRingOutColour;
            }
            if (semantic == 0x3B)
            {
                ++stats.wallTriangles;
                return kWallColour;
            }
            if (semantic == 0x3C)
            {
                ++stats.otherBoundaryTriangles;
                return kOtherBoundaryColour;
            }

            float minY = a.Y;
            if (b.Y < minY) minY = b.Y;
            if (c.Y < minY) minY = c.Y;
            float maxY = a.Y;
            if (b.Y > maxY) maxY = b.Y;
            if (c.Y > maxY) maxY = c.Y;
            if ((maxY - minY) > 0.25f)
            {
                ++stats.wallTriangles;
                return kWallColour;
            }

            ++stats.floorTriangles;
            return kFloorColour;
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
                return 0;

            const FVec3 wa = addFrameOffset(a, frameOffset);
            const FVec3 wb = addFrameOffset(b, frameOffset);
            const FVec3 wc = addFrameOffset(c, frameOffset);
            if (!saneBattlePoint(wa) || !saneBattlePoint(wb) ||
                !saneBattlePoint(wc))
                return 0;

            const FLinColor& colour =
                colourForTerrain(terrainSemantic(primaryFlags,
                                                 secondaryFlags),
                                 wa, wb, wc, stats);
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
            if (p.BoxExtent.X <= 0.0f && p.BoxExtent.Y <= 0.0f &&
                p.BoxExtent.Z <= 0.0f)
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

        void refreshActorBoundsCache(Obj mgr)
        {
            auto* rawManager = mgr.raw();
            if (!rawManager)
            {
                m_cachedManager = nullptr;
                m_breakableWallComponents.clear();
                m_barrierComponents.clear();
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
                m_breakableWallComponents.clear();
                m_barrierComponents.clear();
                m_actorCacheValid = false;
                m_lastActorCacheRebuild = Clock::time_point{};
                return;
            }

            m_cachedManager = rawManager;
            m_breakableWallComponents.clear();
            m_barrierComponents.clear();
            addActorListComponents(mgr, L"BreakableWallActorList",
                                   m_breakableWallComponents);
            addActorListComponents(mgr, L"BarrierActorList",
                                   m_barrierComponents);
            m_actorCacheValid = true;
            m_lastActorCacheRebuild = now;
        }

        void addActorListComponents(Obj mgr,
                                    const wchar_t* propertyName,
                                    std::vector<RC::Unreal::UObject*>& out)
        {
            const TArrHdr* arr = mgr.getPtr<TArrHdr>(propertyName);
            if (!arr || !arr->Data || arr->Num <= 0)
                return;

            const int count = (arr->Num < kMaxActorListCount)
                ? arr->Num
                : kMaxActorListCount;
            auto** actors = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
            {
                Obj actor{actors[i]};
                if (!actor || !isReal(actor.raw()))
                    continue;
                addKnownComponentPointers(actor, out);
            }
        }

        void addKnownComponentPointers(
            Obj actor,
            std::vector<RC::Unreal::UObject*>& out)
        {
            static constexpr const wchar_t* kComponentNames[] = {
                L"BaseMeshComponent",
                L"BaseTranslucentMeshComponent",
                L"BrokenMeshComponent",
                L"BrokenTranslucentMeshComponent",
                L"BreakingMeshComponent",
                L"BreakingTranslucentMeshComponent",
                L"BarrierFace",
                L"BarrierBack",
                L"BarrierFloor",
                L"BarrierBreaking",
            };

            for (const wchar_t* name : kComponentNames)
            {
                Obj component = actor.getObj(name);
                addUnique(out, component.raw());
            }
        }

        int drawCachedComponentBounds(
            const std::vector<RC::Unreal::UObject*>& components,
            LineBatcherBackend& backend,
            const FLinColor& colour)
        {
            int drawn = 0;
            for (auto* component : components)
            {
                Bounds b{};
                if (getComponentBounds(Obj{component}, b))
                    drawn += drawAabb(backend, b.origin, b.extent, colour,
                                      kActorThickness);
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

        static bool readLegacyBarrierValid()
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base) return false;
            uint32_t valid = 0;
            return SafeReadUInt32(reinterpret_cast<const void*>(
                                      base + kLegacyBarrierValidRVA),
                                  &valid) &&
                   valid != 0;
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
                STR("[HorseMod.StageBoundary] grid={} floorTris={} ringTris={} wallTris={} "
                    "otherTris={} terrainLines={} breakableWallLines={} "
                    "barrierLines={} legacyScbattleBarrier={} clipped={}\n"),
                stats.gridValid ? 1 : 0,
                stats.floorTriangles,
                stats.ringTriangles,
                stats.wallTriangles,
                stats.otherBoundaryTriangles,
                stats.terrain,
                stats.breakableWalls,
                stats.breakableBarriers,
                stats.legacyBarrierValid ? 1 : 0,
                stats.terrainClipped ? 1 : 0);
        }
    };
}
