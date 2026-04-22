#include "LuxTraceComponent.h"

ULuxTraceComponent::ULuxTraceComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->TraceColorPalletDataAsset = NULL;
    this->TraceDataAsset = NULL;
    this->OwnerActor = NULL;
    this->CharaAttachComponent = NULL;
    this->WeaponAttachComponent = NULL;
}

void ULuxTraceComponent::OnVFxFinished(int32 InstanceID) {
}


