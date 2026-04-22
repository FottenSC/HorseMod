#include "EnvQueryInstanceBlueprintWrapper.h"

UEnvQueryInstanceBlueprintWrapper::UEnvQueryInstanceBlueprintWrapper() {
    this->QueryID = -1;
    this->ItemType = NULL;
    this->OptionIndex = 0;
}

void UEnvQueryInstanceBlueprintWrapper::SetNamedParam(FName ParamName, float Value) {
}

TArray<FVector> UEnvQueryInstanceBlueprintWrapper::GetResultsAsLocations() {
    return TArray<FVector>();
}

TArray<AActor*> UEnvQueryInstanceBlueprintWrapper::GetResultsAsActors() {
    return TArray<AActor*>();
}

float UEnvQueryInstanceBlueprintWrapper::GetItemScore(int32 ItemIndex) {
    return 0.0f;
}


