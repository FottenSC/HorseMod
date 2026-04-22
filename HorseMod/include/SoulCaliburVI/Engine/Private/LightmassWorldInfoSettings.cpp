#include "LightmassWorldInfoSettings.h"

FLightmassWorldInfoSettings::FLightmassWorldInfoSettings() {
    this->StaticLightingLevelScale = 0.00f;
    this->NumIndirectLightingBounces = 0;
    this->IndirectLightingQuality = 0.00f;
    this->IndirectLightingSmoothness = 0.00f;
    this->EnvironmentIntensity = 0.00f;
    this->EmissiveBoost = 0.00f;
    this->DiffuseBoost = 0.00f;
    this->bUseAmbientOcclusion = false;
    this->bGenerateAmbientOcclusionMaterialMask = false;
    this->DirectIlluminationOcclusionFraction = 0.00f;
    this->IndirectIlluminationOcclusionFraction = 0.00f;
    this->OcclusionExponent = 0.00f;
    this->FullyOccludedSamplesFraction = 0.00f;
    this->MaxOcclusionDistance = 0.00f;
    this->bVisualizeMaterialDiffuse = false;
    this->bVisualizeAmbientOcclusion = false;
    this->VolumeLightSamplePlacementScale = 0.00f;
    this->bCompressLightmaps = false;
}

