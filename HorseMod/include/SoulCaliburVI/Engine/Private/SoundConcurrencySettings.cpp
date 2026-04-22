#include "SoundConcurrencySettings.h"

FSoundConcurrencySettings::FSoundConcurrencySettings() {
    this->MaxCount = 0;
    this->bLimitToOwner = false;
    this->ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
    this->VolumeScale = 0.00f;
}

