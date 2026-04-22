#include "Level.h"

ULevel::ULevel() {
    this->OwningWorld = NULL;
    this->Model = NULL;
    this->ActorCluster = NULL;
    this->NumTextureStreamingUnbuiltComponents = 0;
    this->NumTextureStreamingDirtyResources = 0;
    this->LevelScriptActor = NULL;
    this->NavListStart = NULL;
    this->NavListEnd = NULL;
    this->LightmapTotalSize = 0.00f;
    this->ShadowmapTotalSize = 0.00f;
    this->bIsLightingScenario = false;
    this->MapBuildData = NULL;
    this->bTextureStreamingRotationChanged = false;
    this->bIsVisible = false;
    this->bLocked = false;
    this->WorldSettings = NULL;
}


