#include "WorldSettings.h"
#include "DefaultPhysicsVolume.h"
#include "GameNetworkManager.h"
#include "Net/UnrealNetwork.h"

AWorldSettings::AWorldSettings(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bHidden = false;
    this->bAlwaysRelevant = true;
    this->bReplicates = true;
    const FProperty* p_RemoteRole = GetClass()->FindPropertyByName("RemoteRole");
    (*p_RemoteRole->ContainerPtrToValuePtr<TEnumAsByte<ENetRole>>(this)) = ROLE_SimulatedProxy;
    this->bEnableWorldBoundsChecks = true;
    this->bEnableNavigationSystem = true;
    this->bEnableAISystem = true;
    this->bEnableWorldComposition = false;
    this->bUseClientSideLevelStreamingVolumes = false;
    this->bEnableWorldOriginRebasing = false;
    this->bWorldGravitySet = false;
    this->bGlobalGravitySet = false;
    this->KillZ = -1048575.00f;
    this->WorldGravityZ = 0.00f;
    this->GlobalGravityZ = 0.00f;
    this->DefaultPhysicsVolumeClass = ADefaultPhysicsVolume::StaticClass();
    this->PhysicsCollisionHandlerClass = NULL;
    this->DefaultGameMode = NULL;
    this->GameNetworkManagerClass = AGameNetworkManager::StaticClass();
    this->PackedLightAndShadowMapTextureSize = 1024;
    this->bMinimizeBSPSections = false;
    this->DefaultMaxDistanceFieldOcclusionDistance = 600.00f;
    this->GlobalDistanceFieldViewDistance = 20000.00f;
    this->DynamicIndirectShadowsSelfShadowingIntensity = 0.80f;
    this->bPrecomputeVisibility = false;
    this->bPlaceCellsOnlyAlongCameraTracks = false;
    this->VisibilityCellSize = 200;
    this->VisibilityAggressiveness = VIS_LeastAggressive;
    this->bForceNoPrecomputedLighting = false;
    this->DefaultBaseSoundMix = NULL;
    this->WorldToMeters = 100.00f;
    this->MonoCullingDistance = 750.00f;
    this->BookMarks[0] = NULL;
    this->BookMarks[1] = NULL;
    this->BookMarks[2] = NULL;
    this->BookMarks[3] = NULL;
    this->BookMarks[4] = NULL;
    this->BookMarks[5] = NULL;
    this->BookMarks[6] = NULL;
    this->BookMarks[7] = NULL;
    this->BookMarks[8] = NULL;
    this->BookMarks[9] = NULL;
    this->TimeDilation = 1.00f;
    this->MatineeTimeDilation = 1.00f;
    this->DemoPlayTimeDilation = 1.00f;
    this->MinGlobalTimeDilation = 0.00f;
    this->MaxGlobalTimeDilation = 20.00f;
    this->MinUndilatedFrameTime = 0.00f;
    this->MaxUndilatedFrameTime = 0.40f;
    this->Pauser = NULL;
    this->bHighPriorityLoading = false;
    this->bHighPriorityLoadingLocal = false;
}

void AWorldSettings::OnRep_WorldGravityZ() {
}

void AWorldSettings::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(AWorldSettings, WorldGravityZ);
    DOREPLIFETIME(AWorldSettings, TimeDilation);
    DOREPLIFETIME(AWorldSettings, MatineeTimeDilation);
    DOREPLIFETIME(AWorldSettings, Pauser);
    DOREPLIFETIME(AWorldSettings, bHighPriorityLoading);
}


