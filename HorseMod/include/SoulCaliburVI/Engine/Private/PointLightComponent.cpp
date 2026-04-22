#include "PointLightComponent.h"

UPointLightComponent::UPointLightComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Radius = 1024.00f;
    this->AttenuationRadius = 1000.00f;
    this->bUseInverseSquaredFalloff = true;
    this->LightFalloffExponent = 8.00f;
    this->SourceRadius = 0.00f;
    this->SourceLength = 0.00f;
}

void UPointLightComponent::SetSourceRadius(float bNewValue) {
}

void UPointLightComponent::SetSourceLength(float NewValue) {
}

void UPointLightComponent::SetLightFalloffExponent(float NewLightFalloffExponent) {
}

void UPointLightComponent::SetAttenuationRadius(float NewRadius) {
}


