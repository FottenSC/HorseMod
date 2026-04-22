#include "AudioComponent.h"

UAudioComponent::UAudioComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->bUseAttachParentBound = true;
    this->Sound = NULL;
    this->SoundClassOverride = NULL;
    this->bAutoDestroy = false;
    this->bStopWhenOwnerDestroyed = true;
    this->bShouldRemainActiveIfDropped = false;
    this->bAllowSpatialization = true;
    this->bOverrideAttenuation = false;
    this->bOverrideSubtitlePriority = false;
    this->bIsUISound = false;
    this->bEnableLowPassFilter = false;
    this->bOverridePriority = false;
    this->bSuppressSubtitles = false;
    this->PitchModulationMin = 1.00f;
    this->PitchModulationMax = 1.00f;
    this->VolumeModulationMin = 1.00f;
    this->VolumeModulationMax = 1.00f;
    this->VolumeMultiplier = 1.00f;
    this->Priority = 1.00f;
    this->SubtitlePriority = 10000.00f;
    this->VolumeWeightedPriorityScale = 0.00f;
    this->PitchMultiplier = 1.00f;
    this->HighFrequencyGainMultiplier = 0.00f;
    this->LowPassFilterFrequency = 20000.00f;
    this->AttenuationSettings = NULL;
    this->ConcurrencySettings = NULL;
}

void UAudioComponent::Stop() {
}

void UAudioComponent::SetWaveParameter(FName inName, USoundWave* InWave) {
}

void UAudioComponent::SetVolumeMultiplier(float NewVolumeMultiplier) {
}

void UAudioComponent::SetUISound(bool bInUISound) {
}

void UAudioComponent::SetSubmixSend(USoundSubmix* Submix, float SendLevel) {
}

void UAudioComponent::SetSound(USoundBase* NewSound) {
}

void UAudioComponent::SetPitchMultiplier(float NewPitchMultiplier) {
}

void UAudioComponent::SetPaused(bool bPause) {
}

void UAudioComponent::SetLowPassFilterFrequency(float InLowPassFilterFrequency) {
}

void UAudioComponent::SetLowPassFilterEnabled(bool InLowPassFilterEnabled) {
}

void UAudioComponent::SetIntParameter(FName inName, int32 inInt) {
}

void UAudioComponent::SetFloatParameter(FName inName, float inFloat) {
}

void UAudioComponent::SetBoolParameter(FName inName, bool inBool) {
}

void UAudioComponent::Play(float StartTime) {
}

bool UAudioComponent::IsPlaying() const {
    return false;
}

void UAudioComponent::FadeOut(float FadeOutDuration, float FadeVolumeLevel) {
}

void UAudioComponent::FadeIn(float FadeInDuration, float FadeVolumeLevel, float StartTime) {
}

bool UAudioComponent::BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings) {
    return false;
}

void UAudioComponent::AdjustVolume(float AdjustVolumeDuration, float AdjustVolumeLevel) {
}

void UAudioComponent::AdjustAttenuation(const FSoundAttenuationSettings& InAttenuationSettings) {
}


