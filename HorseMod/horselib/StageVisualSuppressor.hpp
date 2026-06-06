// ============================================================================
// Horse::StageVisualSuppressor
//
// Visual-only stage hiding for inspection workflows.  This hides the rendered
// stage mesh actors/components through UE visibility calls while leaving
// gameplay collision, ring-out, wall logic, hitboxes, and LineBatcher overlays
// untouched.
// ============================================================================

#pragma once

#include "HorseLib.hpp"
#include "SafeMemoryRead.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Horse
{
    class StageVisualSuppressor final
    {
    public:
        void tick(bool hideEnabled)
        {
            if (!hideEnabled)
            {
                if (m_lastAppliedHidden)
                    restoreCapturedObjects();
                clearCache();
                m_lastRequestedHidden = false;
                m_lastAppliedHidden = false;
                return;
            }

            Obj manager = battleStageActorManager();
            auto* rawManager = manager.raw();
            if (!rawManager)
            {
                if (m_lastAppliedHidden)
                    restoreCapturedObjects();
                clearCache();
                m_lastRequestedHidden = hideEnabled;
                m_lastAppliedHidden = false;
                return;
            }

            bool rebuilt = false;
            if (!m_cacheValid || rawManager != m_cachedManager)
            {
                if (m_lastAppliedHidden)
                    restoreCapturedObjects();
                rebuildCache(manager);
                rebuilt = true;
            }

            const bool changed = hideEnabled != m_lastRequestedHidden ||
                                 hideEnabled != m_lastAppliedHidden;

            int newDynamicTargets = 0;
            if (hideEnabled && m_lastAppliedHidden && !changed)
                newDynamicTargets = refreshCuttableDynamicComponents(manager);

            if (changed || rebuilt || newDynamicTargets > 0)
            {
                validateCacheObjects();
                applyHidden(true);
                m_lastAppliedHidden = hideEnabled;
            }
            m_lastRequestedHidden = hideEnabled;
        }

        void invalidate()
        {
            if (m_lastAppliedHidden)
                restoreNow();
            clearCache();
            m_lux.invalidate();
        }

        void restoreNow()
        {
            restoreCapturedObjects();
            m_lastRequestedHidden = false;
            m_lastAppliedHidden = false;
        }

    private:
        using Clock = std::chrono::steady_clock;

        static constexpr std::chrono::seconds kLogInterval{5};
        static constexpr int kMaxListEntries = 512;
        static constexpr int kMaxWorldActors = 4096;
        static constexpr uintptr_t kUWorldPersistentLevel = 0x30;
        static constexpr uintptr_t kUWorldLevels = 0x88;
        static constexpr uintptr_t kULevelActors = 0xA0;

        enum class VisualTargetKind
        {
            Actor,
            Component,
        };

        struct CachedVisualTarget
        {
            RC::Unreal::UObject* object = nullptr;
            VisualTargetKind kind = VisualTargetKind::Component;
            bool originalHidden = false;
            bool originalCaptured = false;
            bool hiddenByUs = false;
        };

        struct CacheStats
        {
            int stage = 0;
            int mesh = 0;
            int hideable = 0;
            int cuttable = 0;
            int wall = 0;
            int barrier = 0;
            int switcher = 0;
            int wolf = 0;
            int mob = 0;
            int worldActorsScanned = 0;
            int worldRenderableComponents = 0;
            int skipped = 0;
            int unsupportedMobs = 0;
        };

        struct ApplyStats
        {
            int actorCalls = 0;
            int componentCalls = 0;
            int setMeshHiddenCalls = 0;
            int switcherCalls = 0;
        };

        Lux m_lux{};
        RC::Unreal::UObject* m_cachedManager = nullptr;

        std::vector<CachedVisualTarget> m_visualTargets;
        std::vector<RC::Unreal::UObject*> m_hideableActors;
        std::vector<RC::Unreal::UObject*> m_visibilitySwitchers;
        std::vector<RC::Unreal::UObject*> m_worldScanExcludedActors;
        int m_stageActorCount = 0;
        CacheStats m_cacheStats{};
        ApplyStats m_lastApplyStats{};

        bool m_lastRequestedHidden = false;
        bool m_lastAppliedHidden = false;
        bool m_cacheValid = false;

        Clock::time_point m_lastLog{};

        Fn m_fnSetHiddenInGame;
        Fn m_fnSetActorHiddenInGame;
        Fn m_fnSetMeshHidden;
        Fn m_fnSetEnableVisibilityCheck;
        Fn m_fnGetComponentsByClass;

        struct ComponentClassQuery
        {
            const wchar_t* path = nullptr;
            RC::Unreal::UObject* klass = nullptr;
        };

        std::array<ComponentClassQuery, 5> m_worldComponentClasses{{
            {L"/Script/Engine.StaticMeshComponent", nullptr},
            {L"/Script/Engine.InstancedStaticMeshComponent", nullptr},
            {L"/Script/Engine.DecalComponent", nullptr},
            {L"/Script/Landscape.LandscapeComponent", nullptr},
            {L"/Script/ProceduralMeshComponent.ProceduralMeshComponent", nullptr},
        }};

        Obj battleStageActorManager()
        {
            Obj bm = m_lux.battleManager();
            if (!bm) return {};
            return bm.getObj(L"BattleStageActorManager");
        }

        void clearCache()
        {
            m_cachedManager = nullptr;
            m_visualTargets.clear();
            m_hideableActors.clear();
            m_visibilitySwitchers.clear();
            m_worldScanExcludedActors.clear();
            m_stageActorCount = 0;
            m_cacheStats = CacheStats{};
            m_cacheValid = false;
        }

        static bool isReal(RC::Unreal::UObject* obj)
        {
            return obj && RC::Unreal::UObject::IsReal(obj);
        }

        static bool readHiddenBit(RC::Unreal::UObject* object,
                                  const wchar_t* propertyName,
                                  bool& out)
        {
            if (!object)
                return false;
            auto* prop = RC::Unreal::CastField<RC::Unreal::FBoolProperty>(
                object->GetPropertyByNameInChain(propertyName));
            if (!prop)
                return false;
            out = prop->GetPropertyValueInContainer(object);
            return true;
        }

        static void captureOriginal(CachedVisualTarget& entry,
                                    const wchar_t* propertyName)
        {
            if (entry.originalCaptured)
                return;
            bool hidden = false;
            if (readHiddenBit(entry.object, propertyName, hidden))
            {
                entry.originalHidden = hidden;
                entry.originalCaptured = true;
            }
        }

        void validateCacheObjects()
        {
            auto stillRealTarget = [](const CachedVisualTarget& entry)
            {
                return isReal(entry.object);
            };
            auto stillRealObject = [](RC::Unreal::UObject* obj)
            {
                return isReal(obj);
            };

            m_visualTargets.erase(
                std::remove_if(m_visualTargets.begin(),
                               m_visualTargets.end(),
                               [&](const CachedVisualTarget& entry)
                               {
                                   return !stillRealTarget(entry);
                               }),
                m_visualTargets.end());

            m_hideableActors.erase(
                std::remove_if(m_hideableActors.begin(),
                               m_hideableActors.end(),
                               [&](RC::Unreal::UObject* obj)
                               {
                                   return !stillRealObject(obj);
                               }),
                m_hideableActors.end());

            m_visibilitySwitchers.erase(
                std::remove_if(m_visibilitySwitchers.begin(),
                               m_visibilitySwitchers.end(),
                               [&](RC::Unreal::UObject* obj)
                               {
                                   return !stillRealObject(obj);
                               }),
                m_visibilitySwitchers.end());
        }

        bool addUniqueTarget(RC::Unreal::UObject* obj,
                             VisualTargetKind kind)
        {
            if (!isReal(obj))
                return false;
            for (const auto& existing : m_visualTargets)
            {
                if (existing.object == obj && existing.kind == kind)
                    return false;
            }
            m_visualTargets.push_back(CachedVisualTarget{obj, kind});
            return true;
        }

        static bool addUniqueObject(std::vector<RC::Unreal::UObject*>& v,
                                    RC::Unreal::UObject* obj)
        {
            if (!isReal(obj))
                return false;
            for (auto* existing : v)
            {
                if (existing == obj)
                    return false;
            }
            v.push_back(obj);
            return true;
        }

        void rebuildCache(Obj manager)
        {
            clearCache();
            if (!manager)
                return;

            m_cachedManager = manager.raw();
            addActorList(manager, L"StageActorList", m_cacheStats.stage);
            addActorList(manager, L"StageMeshActorList", m_cacheStats.mesh);
            addActorList(manager, L"HideableMeshActorList",
                         m_cacheStats.hideable, ActorListRole::Hideable);
            addActorList(manager, L"CuttableStageMeshActorList",
                         m_cacheStats.cuttable);
            addActorList(manager, L"BreakableWallActorList",
                         m_cacheStats.wall);
            addActorList(manager, L"BarrierActorList",
                         m_cacheStats.barrier);
            addActorList(manager, L"WolfCharacterList", m_cacheStats.wolf);
            addActorList(manager, L"VisibilitySwitcherList",
                         m_cacheStats.switcher, ActorListRole::Switcher);
            addStageMobList(manager);
            addWorldRenderableComponents(manager);
            m_cacheValid = true;
            logCacheRebuild();
        }

        enum class ActorListRole
        {
            Default,
            Hideable,
            Switcher,
        };

        void addActorList(Obj manager, const wchar_t* propertyName,
                          int& outCount,
                          ActorListRole role = ActorListRole::Default)
        {
            const TArrHdr* arr = manager.getPtr<TArrHdr>(propertyName);
            if (!arr || !arr->Data || arr->Num <= 0)
                return;

            const int count = (arr->Num < kMaxListEntries)
                ? arr->Num
                : kMaxListEntries;
            auto** actors = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
            {
                Obj actor{actors[i]};
                if (!actor || !isReal(actor.raw()))
                {
                    ++m_cacheStats.skipped;
                    continue;
                }
                ++outCount;
                ++m_stageActorCount;
                addUniqueTarget(actor.raw(), VisualTargetKind::Actor);
                addActorRootComponent(actor);
                addKnownComponents(actor);
                if (role == ActorListRole::Hideable)
                    addUniqueObject(m_hideableActors, actor.raw());
                else if (role == ActorListRole::Switcher)
                {
                    addUniqueObject(m_visibilitySwitchers, actor.raw());
                    addSwitcherActors(actor);
                }
            }
        }

        void addStageMobList(Obj manager)
        {
            const TArrHdr* arr = manager.getPtr<TArrHdr>(L"StageMobList");
            if (!arr || !arr->Data || arr->Num <= 0)
                return;

            const int count = (arr->Num < kMaxListEntries)
                ? arr->Num
                : kMaxListEntries;
            auto** mobs = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
            {
                Obj mob{mobs[i]};
                if (!mob || !isReal(mob.raw()))
                {
                    ++m_cacheStats.skipped;
                    continue;
                }
                ++m_cacheStats.mob;
                if (!addStageMobOwner(mob))
                    ++m_cacheStats.unsupportedMobs;
            }
        }

        bool addStageMobOwner(Obj mob)
        {
            static constexpr const wchar_t* kOwnerNames[] = {
                L"Owner",
                L"OwingActor",
                L"OwningActor",
                L"SkelMeshComponent",
                L"SkeletalMeshComponent",
                L"Mesh",
            };

            bool added = false;
            for (const wchar_t* name : kOwnerNames)
            {
                Obj owner = mob.getObj(name);
                if (!owner || !isReal(owner.raw()))
                    continue;
                added = addUniqueTarget(owner.raw(), VisualTargetKind::Actor)
                    || added;
                addActorRootComponent(owner);
                addKnownComponents(owner);
            }
            return added;
        }

        void addActorRootComponent(Obj actor)
        {
            Obj root = actor.getObj(L"RootComponent");
            addUniqueTarget(root.raw(), VisualTargetKind::Component);
        }

        void addSwitcherActors(Obj switcher)
        {
            const TArrHdr* arr = switcher.getPtr<TArrHdr>(L"ActorList");
            if (!arr || !arr->Data || arr->Num <= 0)
                return;

            const int count = (arr->Num < kMaxListEntries)
                ? arr->Num
                : kMaxListEntries;
            auto** actors = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
            {
                Obj actor{actors[i]};
                if (!actor || !isReal(actor.raw()))
                {
                    ++m_cacheStats.skipped;
                    continue;
                }
                addUniqueTarget(actor.raw(), VisualTargetKind::Actor);
                addActorRootComponent(actor);
                addKnownComponents(actor);
            }
        }

        void addKnownComponents(Obj actor)
        {
            static constexpr const wchar_t* kComponentNames[] = {
                L"BaseMeshComponent",
                L"SoftOpacityMeshComponent",
                L"TranslucentMeshComponent",
                L"DitherMeshComponent",
                L"MeshComponent",
                L"ProceduralMeshComponent",
                L"Cutted_ProceduralMeshComponent",
                L"BaseTranslucentMeshComponent",
                L"BrokenMeshComponent",
                L"BrokenTranslucentMeshComponent",
                L"BreakingMeshComponent",
                L"BreakingTranslucentMeshComponent",
                L"BarrierFace",
                L"BarrierBack",
                L"BarrierFloor",
                L"BarrierBreaking",
                L"LuxSkeletalMeshComponent",
                L"ParticleSystemComponent",
                L"HitEffect",
                L"BreakEffect",
            };

            for (const wchar_t* name : kComponentNames)
            {
                Obj component = actor.getObj(name);
                addUniqueTarget(component.raw(), VisualTargetKind::Component);
            }

            addComponentArray(actor, L"Cutted_Meshes");
        }

        void addDynamicCuttableComponents(Obj actor)
        {
            Obj cutted = actor.getObj(L"Cutted_ProceduralMeshComponent");
            addUniqueTarget(cutted.raw(), VisualTargetKind::Component);
            addComponentArray(actor, L"Cutted_Meshes");
        }

        void resolveWorldComponentClasses()
        {
            using namespace RC::Unreal;
            for (auto& query : m_worldComponentClasses)
            {
                if (query.klass && UObject::IsReal(query.klass))
                    continue;
                query.klass = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, query.path);
            }
        }

        void buildWorldScanExclusions(Obj manager)
        {
            m_worldScanExcludedActors.clear();
            addUniqueObject(m_worldScanExcludedActors, manager.raw());

            Obj bm = m_lux.battleManager();
            addUniqueObject(m_worldScanExcludedActors, bm.raw());

            Obj cockpit = m_lux.cockpit();
            addUniqueObject(m_worldScanExcludedActors, cockpit.raw());

            const TArrHdr* chars = m_lux.battleCharaArray();
            if (!chars || !chars->Data || chars->Num <= 0 ||
                chars->Num > kMaxListEntries)
            {
                return;
            }

            auto** charaPtrs =
                static_cast<RC::Unreal::UObject**>(chars->Data);
            for (int i = 0; i < chars->Num; ++i)
                addUniqueObject(m_worldScanExcludedActors, charaPtrs[i]);
        }

        bool isWorldScanExcludedActor(RC::Unreal::UObject* actor) const
        {
            if (!actor)
                return true;
            for (auto* excluded : m_worldScanExcludedActors)
            {
                if (excluded == actor)
                    return true;
            }
            return false;
        }

        void addWorldRenderableComponents(Obj manager)
        {
            if (!manager || !manager.raw())
                return;

            resolveWorldComponentClasses();
            buildWorldScanExclusions(manager);

            auto* world = manager.raw()->GetWorld();
            if (!world)
                return;

            void* persistentLevel = nullptr;
            if (SafeReadPtr(reinterpret_cast<const uint8_t*>(world) +
                            kUWorldPersistentLevel, &persistentLevel))
            {
                addLevelRenderableComponents(
                    static_cast<RC::Unreal::UObject*>(persistentLevel));
            }

            TArrHdr levels{};
            if (!SafeReadBytes(reinterpret_cast<const uint8_t*>(world) +
                               kUWorldLevels, &levels, sizeof(levels)) ||
                !saneObjectArray(levels, kMaxListEntries))
            {
                return;
            }

            auto** levelPtrs =
                static_cast<RC::Unreal::UObject**>(levels.Data);
            for (int i = 0; i < levels.Num; ++i)
            {
                void* level = nullptr;
                if (!SafeReadPtr(levelPtrs + i, &level))
                {
                    ++m_cacheStats.skipped;
                    continue;
                }
                addLevelRenderableComponents(
                    static_cast<RC::Unreal::UObject*>(level));
            }
        }

        static bool saneObjectArray(const TArrHdr& arr, int maxEntries)
        {
            if (arr.Num < 0 || arr.Max < arr.Num || arr.Num > maxEntries)
                return false;
            return arr.Num == 0 || arr.Data != nullptr;
        }

        void addLevelRenderableComponents(RC::Unreal::UObject* level)
        {
            if (!isReal(level))
                return;

            TArrHdr actors{};
            if (!SafeReadBytes(reinterpret_cast<const uint8_t*>(level) +
                               kULevelActors, &actors, sizeof(actors)) ||
                !saneObjectArray(actors, kMaxWorldActors))
            {
                return;
            }

            auto** actorPtrs =
                static_cast<RC::Unreal::UObject**>(actors.Data);
            for (int i = 0; i < actors.Num; ++i)
            {
                void* rawActor = nullptr;
                if (!SafeReadPtr(actorPtrs + i, &rawActor))
                {
                    ++m_cacheStats.skipped;
                    continue;
                }

                auto* actor = static_cast<RC::Unreal::UObject*>(rawActor);
                if (!isReal(actor) || isWorldScanExcludedActor(actor))
                    continue;

                ++m_cacheStats.worldActorsScanned;
                addActorRenderableComponents(Obj{actor});
            }
        }

        void addActorRenderableComponents(Obj actor)
        {
            if (!actor)
                return;

            for (const auto& query : m_worldComponentClasses)
            {
                if (!isReal(query.klass))
                    continue;

                struct Params
                {
                    RC::Unreal::UObject* ComponentClass;
                    TArrHdr ReturnValue;
                } p{query.klass, {}};

                actor.callRaw(m_fnGetComponentsByClass,
                              L"GetComponentsByClass", &p);
                if (!saneObjectArray(p.ReturnValue, kMaxListEntries))
                {
                    freeReturnedArray(p.ReturnValue);
                    continue;
                }

                auto** components =
                    static_cast<RC::Unreal::UObject**>(p.ReturnValue.Data);
                for (int i = 0; i < p.ReturnValue.Num; ++i)
                {
                    void* component = nullptr;
                    if (!SafeReadPtr(components + i, &component))
                    {
                        ++m_cacheStats.skipped;
                        continue;
                    }
                    if (addUniqueTarget(
                            static_cast<RC::Unreal::UObject*>(component),
                            VisualTargetKind::Component))
                    {
                        ++m_cacheStats.worldRenderableComponents;
                    }
                }
                freeReturnedArray(p.ReturnValue);
            }
        }

        static void freeReturnedArray(TArrHdr& arr)
        {
            if (!arr.Data)
                return;
            if (RC::Unreal::GMalloc && *RC::Unreal::GMalloc)
                (*RC::Unreal::GMalloc)->Free(arr.Data);
            arr.Data = nullptr;
            arr.Num = 0;
            arr.Max = 0;
        }

        int refreshCuttableDynamicComponents(Obj manager)
        {
            const size_t before = m_visualTargets.size();
            const TArrHdr* arr = manager.getPtr<TArrHdr>(
                L"CuttableStageMeshActorList");
            if (!arr || !arr->Data || arr->Num <= 0)
                return 0;

            const int count = (arr->Num < kMaxListEntries)
                ? arr->Num
                : kMaxListEntries;
            auto** actors = static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
            {
                Obj actor{actors[i]};
                if (!actor || !isReal(actor.raw()))
                    continue;
                addDynamicCuttableComponents(actor);
            }
            return static_cast<int>(m_visualTargets.size() - before);
        }

        void addComponentArray(Obj actor, const wchar_t* propertyName)
        {
            const TArrHdr* arr = actor.getPtr<TArrHdr>(propertyName);
            if (!arr || !arr->Data || arr->Num <= 0)
                return;

            const int count = (arr->Num < kMaxListEntries)
                ? arr->Num
                : kMaxListEntries;
            auto** components =
                static_cast<RC::Unreal::UObject**>(arr->Data);
            for (int i = 0; i < count; ++i)
                addUniqueTarget(components[i], VisualTargetKind::Component);
        }

        void applyHidden(bool hidden)
        {
            ApplyStats stats{};

            applyHideableActors(hidden, stats);
            applyVisibilitySwitchers(hidden, stats);

            for (auto& target : m_visualTargets)
            {
                if (!isReal(target.object))
                    continue;
                applyTargetHidden(target, hidden, stats);
            }

            m_lastApplyStats = stats;
            logApply(hidden, stats);
        }

        void restoreCapturedObjects()
        {
            validateCacheObjects();

            ApplyStats stats{};
            applyHideableActors(false, stats);
            applyVisibilitySwitchers(false, stats);

            for (auto& target : m_visualTargets)
            {
                if (!target.hiddenByUs)
                    continue;
                if (target.originalCaptured && isReal(target.object))
                {
                    callTargetHidden(target, target.originalHidden, stats);
                }
                target.originalHidden = false;
                target.originalCaptured = false;
                target.hiddenByUs = false;
            }
            m_lastApplyStats = stats;
            logApply(false, stats);
        }

        void applyTargetHidden(CachedVisualTarget& target, bool hidden,
                               ApplyStats& stats)
        {
            const wchar_t* propertyName =
                target.kind == VisualTargetKind::Actor
                    ? L"bHidden"
                    : L"bHiddenInGame";

            bool currentHidden = false;
            const bool haveCurrent =
                readHiddenBit(target.object, propertyName, currentHidden);

            if (hidden)
            {
                captureOriginal(target, propertyName);
                if (haveCurrent && currentHidden == hidden)
                    return;
                callTargetHidden(target, hidden, stats);
                target.hiddenByUs = true;
                return;
            }

            if (target.hiddenByUs && target.originalCaptured &&
                (!haveCurrent || currentHidden != target.originalHidden))
            {
                callTargetHidden(target, target.originalHidden, stats);
            }
            target.originalHidden = false;
            target.originalCaptured = false;
            target.hiddenByUs = false;
        }

        void callTargetHidden(const CachedVisualTarget& target, bool hidden,
                              ApplyStats& stats)
        {
            Obj obj{target.object};
            if (target.kind == VisualTargetKind::Actor)
            {
                struct Params
                {
                    bool bNewHidden;
                } p{hidden};
                obj.callRaw(m_fnSetActorHiddenInGame,
                            L"SetActorHiddenInGame", &p);
                ++stats.actorCalls;
                return;
            }

            struct Params
            {
                bool NewHidden;
                bool bPropagateToChildren;
            } p{hidden, true};
            obj.callRaw(m_fnSetHiddenInGame, L"SetHiddenInGame", &p);
            ++stats.componentCalls;
        }

        void applyHideableActors(bool hidden, ApplyStats& stats)
        {
            for (auto* actor : m_hideableActors)
            {
                if (!isReal(actor))
                    continue;
                Obj obj{actor};
                struct Params
                {
                    bool inHidden;
                } p{hidden};
                obj.callRaw(m_fnSetMeshHidden, L"SetMeshHidden", &p);
                ++stats.setMeshHiddenCalls;
            }
        }

        void applyVisibilitySwitchers(bool hidden, ApplyStats& stats)
        {
            for (auto* switcher : m_visibilitySwitchers)
            {
                if (!isReal(switcher))
                    continue;
                Obj obj{switcher};
                struct Params
                {
                    bool Enabled;
                } p{!hidden};
                obj.callRaw(m_fnSetEnableVisibilityCheck,
                            L"SetEnableVisibilityCheck", &p);
                ++stats.switcherCalls;
            }
        }

        void logCacheRebuild()
        {
            const auto now = Clock::now();
            if (m_lastLog.time_since_epoch().count() != 0 &&
                now - m_lastLog < kLogInterval)
                return;
            m_lastLog = now;

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.StageVisuals] cache manager=0x{:x} actors={} "
                    "components={} listEntries={} lists stage={} mesh={} "
                    "hideable={} cuttable={} wall={} barrier={} switcher={} "
                    "wolf={} mob={} worldActors={} worldComponents={} "
                    "skipped={} unsupportedMobs={} hidden={}\n"),
                reinterpret_cast<uintptr_t>(m_cachedManager),
                countTargets(VisualTargetKind::Actor),
                countTargets(VisualTargetKind::Component),
                m_stageActorCount,
                m_cacheStats.stage,
                m_cacheStats.mesh,
                m_cacheStats.hideable,
                m_cacheStats.cuttable,
                m_cacheStats.wall,
                m_cacheStats.barrier,
                m_cacheStats.switcher,
                m_cacheStats.wolf,
                m_cacheStats.mob,
                m_cacheStats.worldActorsScanned,
                m_cacheStats.worldRenderableComponents,
                m_cacheStats.skipped,
                m_cacheStats.unsupportedMobs,
                m_lastAppliedHidden ? 1 : 0);
        }

        int countTargets(VisualTargetKind kind) const
        {
            int count = 0;
            for (const auto& target : m_visualTargets)
            {
                if (target.kind == kind)
                    ++count;
            }
            return count;
        }

        void logApply(bool hidden, const ApplyStats& stats)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[HorseMod.StageVisuals] apply hidden={} actorCalls={} "
                    "componentCalls={} setMeshHiddenCalls={} "
                    "switcherCalls={}\n"),
                hidden ? 1 : 0,
                stats.actorCalls,
                stats.componentCalls,
                stats.setMeshHiddenCalls,
                stats.switcherCalls);
        }
    };
}
