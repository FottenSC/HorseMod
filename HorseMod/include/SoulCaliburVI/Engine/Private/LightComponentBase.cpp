#include "LightComponentBase.h"

ULightComponentBase::ULightComponentBase(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Brightness = 3.14f;
    this->Intensity = 3.14f;
    this->bAffectsWorld = true;
    this->CastShadows = true;
    this->CastStaticShadows = true;
    this->CastDynamicShadows = true;
    this->bAffectTranslucentLighting = false;
    this->bCastVolumetricShadow = false;
    this->IndirectLightingIntensity = 0.00f;
    this->VolumetricScatteringIntensity = 1.00f;
}

void ULightComponentBase::SetCastVolumetricShadow(bool bNewValue) {
}

void ULightComponentBase::SetCastShadows(bool bNewValue) {
}

FLinearColor ULightComponentBase::GetLightColor() const {
    return FLinearColor{};
}


