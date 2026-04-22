#include "SoundAttenuationSettings.h"

FSoundAttenuationSettings::FSoundAttenuationSettings() {
    this->bAttenuate = false;
    this->bSpatialize = false;
    this->bAttenuateWithLPF = false;
    this->bEnableListenerFocus = false;
    this->bEnableOcclusion = false;
    this->bUseComplexCollisionForOcclusion = false;
    this->DistanceType = SOUNDDISTANCE_Normal;
    this->OmniRadius = 0.00f;
    this->StereoSpread = 0.00f;
    this->SpatializationAlgorithm = SPATIALIZATION_Default;
    this->SpatializationPluginSettings = NULL;
    this->RadiusMin = 0.00f;
    this->RadiusMax = 0.00f;
    this->LPFRadiusMin = 0.00f;
    this->LPFRadiusMax = 0.00f;
    this->LPFFrequencyAtMin = 0.00f;
    this->LPFFrequencyAtMax = 0.00f;
    this->FocusAzimuth = 0.00f;
    this->NonFocusAzimuth = 0.00f;
    this->FocusDistanceScale = 0.00f;
    this->NonFocusDistanceScale = 0.00f;
    this->FocusPriorityScale = 0.00f;
    this->NonFocusPriorityScale = 0.00f;
    this->FocusVolumeAttenuation = 0.00f;
    this->NonFocusVolumeAttenuation = 0.00f;
    this->OcclusionTraceChannel = ECC_WorldStatic;
    this->OcclusionLowPassFilterFrequency = 0.00f;
    this->OcclusionVolumeAttenuation = 0.00f;
    this->OcclusionInterpolationTime = 0.00f;
    this->OcclusionPluginSettings = NULL;
    this->ReverbPluginSettings = NULL;
    this->ReverbWetLevelMin = 0.00f;
    this->ReverbWetLevelMax = 0.00f;
    this->ReverbDistanceMin = 0.00f;
    this->ReverbDistanceMax = 0.00f;
}

