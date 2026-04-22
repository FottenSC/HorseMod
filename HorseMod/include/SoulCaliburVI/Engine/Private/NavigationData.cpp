#include "NavigationData.h"

ANavigationData::ANavigationData(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bNetLoadOnClient = false;
    this->bCanBeDamaged = false;
    this->RenderingComp = NULL;
    this->bEnableDrawing = false;
    this->bForceRebuildOnLoad = false;
    this->bCanBeMainNavData = true;
    this->bCanSpawnOnRebuild = true;
    this->bRebuildAtRuntime = false;
    this->RuntimeGeneration = ERuntimeGenerationType::Static;
    this->ObservedPathsTickInterval = 0.50f;
    this->DataVersion = 13;
}


