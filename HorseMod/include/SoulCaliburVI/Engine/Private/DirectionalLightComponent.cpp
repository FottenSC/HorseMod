#include "DirectionalLightComponent.h"

UDirectionalLightComponent::UDirectionalLightComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bCastVolumetricShadow = true;
    this->CastTranslucentShadows = true;
    this->bEnableLightShaftOcclusion = false;
    this->OcclusionMaskDarkness = 0.05f;
    this->OcclusionDepthRange = 100000.00f;
    this->WholeSceneDynamicShadowRadius = 20000.00f;
    this->DynamicShadowDistanceMovableLight = 20000.00f;
    this->DynamicShadowDistanceStationaryLight = 0.00f;
    this->DynamicShadowCascades = 3;
    this->CascadeDistributionExponent = 3.00f;
    this->CascadeTransitionFraction = 0.10f;
    this->ShadowDistanceFadeoutFraction = 0.10f;
    this->bUseInsetShadowsForMovableObjects = true;
    this->FarShadowCascadeCount = 0;
    this->FarShadowDistance = 300000.00f;
    this->DistanceFieldShadowDistance = 30000.00f;
    this->LightSourceAngle = 1.00f;
    this->TraceDistance = 10000.00f;
    this->bCastModulatedShadows = false;
    this->bUsedAsAtmosphereSunLight = false;
}

void UDirectionalLightComponent::SetShadowDistanceFadeoutFraction(float NewValue) {
}

void UDirectionalLightComponent::SetOcclusionMaskDarkness(float NewValue) {
}

void UDirectionalLightComponent::SetLightShaftOverrideDirection(FVector NewValue) {
}

void UDirectionalLightComponent::SetEnableLightShaftOcclusion(bool bNewValue) {
}

void UDirectionalLightComponent::SetDynamicShadowDistanceStationaryLight(float NewValue) {
}

void UDirectionalLightComponent::SetDynamicShadowDistanceMovableLight(float NewValue) {
}

void UDirectionalLightComponent::SetDynamicShadowCascades(int32 NewValue) {
}

void UDirectionalLightComponent::SetCascadeTransitionFraction(float NewValue) {
}

void UDirectionalLightComponent::SetCascadeDistributionExponent(float NewValue) {
}


