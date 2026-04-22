#include "LevelStreaming.h"

ULevelStreaming::ULevelStreaming() {
    this->bShouldBeVisibleInEditor = true;
    this->bLocked = false;
    this->bShouldBeLoaded = false;
    this->bShouldBeVisible = false;
    this->bIsStatic = false;
    this->bShouldBlockOnLoad = false;
    this->LevelLODIndex = -1;
    this->bDisableDistanceStreaming = false;
    this->bDrawOnLevelStatusMap = true;
    this->MinTimeBetweenVolumeUnloadRequests = 2.00f;
    this->LoadedLevel = NULL;
    this->PendingUnloadLevel = NULL;
}

bool ULevelStreaming::IsStreamingStatePending() const {
    return false;
}

bool ULevelStreaming::IsLevelVisible() const {
    return false;
}

bool ULevelStreaming::IsLevelLoaded() const {
    return false;
}

FName ULevelStreaming::GetWorldAssetPackageFName() const {
    return NAME_None;
}

ALevelScriptActor* ULevelStreaming::GetLevelScriptActor() {
    return NULL;
}

ULevelStreaming* ULevelStreaming::CreateInstance(const FString& UniqueInstanceName) {
    return NULL;
}


