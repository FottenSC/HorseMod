#include "NavigationSystem.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=AIModule -ObjectName=CrowdManager -FallbackName=CrowdManager
#include "Templates/SubclassOf.h"

UNavigationSystem::UNavigationSystem() {
    this->MainNavData = NULL;
    this->AbstractNavData = NULL;
    this->CrowdManagerClass = UCrowdManager::StaticClass();
    this->bAutoCreateNavigationData = true;
    this->bAllowClientSideNavigation = false;
    this->bSupportRebuilding = false;
    this->bInitialBuildingLocked = false;
    this->bSkipAgentHeightCheckWhenPickingNavData = false;
    this->DataGatheringMode = ENavDataGatheringModeConfig::Instant;
    this->bGenerateNavigationOnlyAroundNavigationInvokers = false;
    this->ActiveTilesUpdateInterval = 1.00f;
    this->DirtyAreasUpdateFreq = 60.00f;
    this->OperationMode = FNavigationSystemRunMode::InvalidMode;
}

void UNavigationSystem::UnregisterNavigationInvoker(AActor* Invoker) {
}

void UNavigationSystem::SimpleMoveToLocation(AController* Controller, const FVector& Goal) {
}

void UNavigationSystem::SimpleMoveToActor(AController* Controller, const AActor* Goal) {
}

void UNavigationSystem::SetMaxSimultaneousTileGenerationJobsCount(int32 MaxNumberOfJobs) {
}

void UNavigationSystem::SetGeometryGatheringMode(ENavDataGatheringModeConfig NewMode) {
}

void UNavigationSystem::ResetMaxSimultaneousTileGenerationJobsCount() {
}

void UNavigationSystem::RegisterNavigationInvoker(AActor* Invoker, float TileGenerationRadius, float TileRemovalRadius) {
}

FVector UNavigationSystem::ProjectPointToNavigation(UObject* WorldContextObject, const FVector& Point, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass, const FVector QueryExtent) {
    return FVector{};
}

void UNavigationSystem::OnNavigationBoundsUpdated(ANavMeshBoundsVolume* NavVolume) {
}

bool UNavigationSystem::NavigationRaycast(UObject* WorldContextObject, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, TSubclassOf<UNavigationQueryFilter> FilterClass, AController* Querier) {
    return false;
}

bool UNavigationSystem::K2_ProjectPointToNavigation(UObject* WorldContextObject, const FVector& Point, FVector& ProjectedLocation, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass, const FVector QueryExtent) {
    return false;
}

bool UNavigationSystem::K2_GetRandomReachablePointInRadius(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return false;
}

bool UNavigationSystem::K2_GetRandomPointInNavigableRadius(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return false;
}

bool UNavigationSystem::IsNavigationBeingBuiltOrLocked(UObject* WorldContextObject) {
    return false;
}

bool UNavigationSystem::IsNavigationBeingBuilt(UObject* WorldContextObject) {
    return false;
}

FVector UNavigationSystem::GetRandomReachablePointInRadius(UObject* WorldContextObject, const FVector& Origin, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return FVector{};
}

FVector UNavigationSystem::GetRandomPointInNavigableRadius(UObject* WorldContextObject, const FVector& Origin, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return FVector{};
}

TEnumAsByte<ENavigationQueryResult::Type> UNavigationSystem::GetPathLength(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, float& PathLength, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return ENavigationQueryResult::Invalid;
}

TEnumAsByte<ENavigationQueryResult::Type> UNavigationSystem::GetPathCost(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, float& PathCost, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return ENavigationQueryResult::Invalid;
}

UNavigationSystem* UNavigationSystem::GetNavigationSystem(UObject* WorldContextObject) {
    return NULL;
}

UNavigationPath* UNavigationSystem::FindPathToLocationSynchronously(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, AActor* PathfindingContext, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return NULL;
}

UNavigationPath* UNavigationSystem::FindPathToActorSynchronously(UObject* WorldContextObject, const FVector& PathStart, AActor* GoalActor, float TetherDistance, AActor* PathfindingContext, TSubclassOf<UNavigationQueryFilter> FilterClass) {
    return NULL;
}


