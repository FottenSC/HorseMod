#include "LevelStreamingKismet.h"

ULevelStreamingKismet::ULevelStreamingKismet() {
    this->bInitiallyLoaded = false;
    this->bInitiallyVisible = false;
}

ULevelStreamingKismet* ULevelStreamingKismet::LoadLevelInstance(UObject* WorldContextObject, const FString& LevelName, const FVector& Location, const FRotator& Rotation, bool& bOutSuccess) {
    return NULL;
}


