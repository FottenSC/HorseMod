#include "LightComponent.h"

ULightComponent::ULightComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAffectTranslucentLighting = true;
    this->Temperature = 6500.00f;
    this->MaxDrawDistance = 0.00f;
    this->MaxDistanceFadeRange = 0.00f;
    this->bUseTemperature = false;
    this->ShadowMapChannel = 0;
    this->MinRoughness = 0.08f;
    this->ShadowResolutionScale = 1.00f;
    this->ShadowBias = 0.50f;
    this->ShadowSharpen = 0.00f;
    this->ContactShadowLength = 0.00f;
    this->InverseSquaredFalloff = false;
    this->CastTranslucentShadows = false;
    this->bCastShadowsFromCinematicObjectsOnly = false;
    this->bAffectDynamicIndirectLighting = false;
    this->LightFunctionMaterial = NULL;
    this->IESTexture = NULL;
    this->bUseIESBrightness = false;
    this->IESBrightnessScale = 1.00f;
    this->LightFunctionFadeDistance = 100000.00f;
    this->DisabledBrightness = 0.50f;
    this->bEnableLightShaftBloom = false;
    this->BloomScale = 0.20f;
    this->BloomThreshold = 0.00f;
    this->bUseRayTracedDistanceFieldShadows = false;
    this->RayStartOffsetDepthScale = 0.00f;
}

void ULightComponent::SetVolumetricScatteringIntensity(float NewIntensity) {
}

void ULightComponent::SetTemperature(float NewTemperature) {
}

void ULightComponent::SetShadowBias(float NewValue) {
}

void ULightComponent::SetLightFunctionScale(FVector NewLightFunctionScale) {
}

void ULightComponent::SetLightFunctionMaterial(UMaterialInterface* NewLightFunctionMaterial) {
}

void ULightComponent::SetLightFunctionFadeDistance(float NewLightFunctionFadeDistance) {
}

void ULightComponent::SetLightFunctionDisabledBrightness(float NewValue) {
}

void ULightComponent::SetLightColor(FLinearColor NewLightColor, bool bSRGB) {
}

void ULightComponent::SetIntensity(float NewIntensity) {
}

void ULightComponent::SetIndirectLightingIntensity(float NewIntensity) {
}

void ULightComponent::SetIESTexture(UTextureLightProfile* NewValue) {
}

void ULightComponent::SetEnableLightShaftBloom(bool bNewValue) {
}

void ULightComponent::SetBloomTint(FColor NewValue) {
}

void ULightComponent::SetBloomThreshold(float NewValue) {
}

void ULightComponent::SetBloomScale(float NewValue) {
}

void ULightComponent::SetAffectTranslucentLighting(bool bNewValue) {
}

void ULightComponent::SetAffectDynamicIndirectLighting(bool bNewValue) {
}


